/*
        discovery.c: how this machine can be reached, and how it reaches out.

        The network stack's discovery module (docs/discovery.md). It knows
        nothing of the link: it reads the person's switches, makes the
        lookups they allow, keeps what it found and says it, so waterlink,
        `moonwater ssh`, the update fetcher and the time sync all ask it the
        same way. `moonwater network` (src/sh/host/network.c) is its verb.

        What it gathers:

          every address of every interface, IPv4 and IPv6, with its scope
          (global, private, shared, link-local, unique local) and the
          interface's kind (ethernet, wifi, vpn, virtual, loopback), read
          from the kernel's own table by netlink and sending nothing;

          whether UDP gets out, the public IPv4 address and the NAT's
          mapping, by a STUN binding (RFC 8489) from one socket to the first
          two servers of the person's list that resolve (stun.cloudflare.com
          and stun.l.google.com unless another is kept): one mapping for
          both is a NAT that maps by endpoint (RFC 4787's
          endpoint-independent mapping, which a hole can be punched
          through), two mappings is one that maps per server;

          where UDP does not get out, the public address over HTTPS, the
          trace at the URL kept (Cloudflare's unless another is), fetched
          and read in a confined child (net_apart) as the zone question is;

          whether an HTTP proxy is named (HTTPS_PROXY), unless the person
          said to ignore it, which a later piece tunnels through by CONNECT.

        Every lookup that sends a packet off the machine has a switch, a
        word in /root that is on when there is none, and so do the local
        network's announcing and asking; discovery's own switch is all of
        them at once (discovery_settings). Turning one on is a deliberate
        act in Settings, turning one off never is. A lookup that is not
        allowed sends nothing: discovery_sent counts every datagram and
        connection this file opens, and the check holds it where it was with
        discovery off; the local network's socket refuses a packet whose
        switch is off before it counts it (discovery_lan_allowed). Every wait
        has a time limit the person may change, about a second by default.
        What a gather finds is kept with its time in DISCOVERY_CACHE_PATH,
        which is root's and in /run, so a reader (Settings, the status)
        never waits on the network, and discovery_report says it a line a
        key with the value alone, and how and when on lines of their own, to
        any writer. A gather is made when asked, and by the network's
        watcher at its start and when the network changes (src/sh/net.c's
        net_discovery_soon), apart from the lease and only while a lookup is
        on.

        IPv6's public address is the interface's own global address for now:
        the resolver asks A records alone and the confined child has no
        IPv6 socket, so STUN and the trace over IPv6 come with them.

        And the local network, which is discovery's too: mDNS and DNS-SD as
        records (a transform any service can announce by, waterlink's groups
        the first), the socket they travel on, and the confined reader of
        what anyone on the link sends.

        Included after net.c: by src/sh/net.c in the shell, and by
        src/waterlink/group.c where the link is built without one (a Mac),
        after the headers its listener includes there. That is why Darwin's
        side of this file is stubs for STUN, the trace and the netlink walk,
        libc's calls for the local network's socket, and nothing of the
        shell's is named here.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef NET_DISCOVERY_INCLUDED
#define NET_DISCOVERY_INCLUDED

#include "../sh/host_words.h"

//      What discovery may send off the machine, each by the person's switch:
//      a gather's two lookups, and the local network's announcing and asking.
#define DISCOVERY_ASK_STUN 1u
#define DISCOVERY_ASK_HTTPS 2u
#define DISCOVERY_ASK_ANNOUNCE 4u
#define DISCOVERY_ASK_LISTEN 8u

#define DISCOVERY_CACHE_PATH "/run/moonwater/discovery"
#define DISCOVERY_STUN_PORT 3478

static positive discovery_sent;

/* ---- the pure parts: kinds, scopes, text, STUN, the trace, the NAT ---- */

#define DISCOVERY_KIND_OTHER 0
#define DISCOVERY_KIND_ETHERNET 1
#define DISCOVERY_KIND_WIFI 2
#define DISCOVERY_KIND_VPN 3
#define DISCOVERY_KIND_VIRTUAL 4
#define DISCOVERY_KIND_LOOPBACK 5
static const string_address discovery_kind_names[] = {
    "other", "ethernet", "wifi", "vpn", "virtual", "loopback"};

//      Whether a name starts with a prefix a table holds (string_has_prefix
//      is for a literal).
static p8 discovery_starts(string_address text, string_address prefix)
{
        positive length = string_length(prefix);

        return text && string_length(text) >= length &&
               !memory_compare(text, prefix, length);
}

/*      An interface's kind, from its name, its hardware type (ARPHRD_*), the
        kernel's word for its driver (netlink_link_driver, or none) and
        whether it is a wireless one, as the lease's choice of a link tells
        it (netlink_link_is_wireless). A VPN is told by its driver or its
        name before its hardware: ZeroTier's is an ethernet tap. */
static positive discovery_interface_kind(string_address name, p16 type,
                                          p8 wireless, string_address driver)
{
        static const string_address vpn_names[] = {
            "wg", "tailscale", "zt", "tun", "utun", "ppp", "ipsec",
            "nordlynx", "proton", "mullvad", "tap"};
        static const string_address vpn_drivers[] = {
            "wireguard", "tun", "ipip", "gre", "sit", "vti", "xfrm", "ppp"};
        static const string_address virtual_names[] = {
            "docker", "br-", "virbr", "veth", "vnet", "lxc", "cni", "podman"};
        static const string_address virtual_drivers[] = {
            "bridge", "veth", "macvlan", "ipvlan", "vxlan", "dummy", "bond"};

        if (type == 772 || (name && string_equals(name, "lo")))
                return DISCOVERY_KIND_LOOPBACK;
        for (positive at = 0; driver && at < array_count(vpn_drivers); at++)
                if (string_equals(driver, vpn_drivers[at]))
                        return DISCOVERY_KIND_VPN;
        for (positive at = 0; at < array_count(vpn_names); at++)
                if (discovery_starts(name, vpn_names[at]))
                        return DISCOVERY_KIND_VPN;
        if (type == 65534 || type == 512 || type == 768 || type == 776 ||
            type == 778)
                return DISCOVERY_KIND_VPN;
        if (wireless)
                return DISCOVERY_KIND_WIFI;
        for (positive at = 0; driver && at < array_count(virtual_drivers); at++)
                if (string_equals(driver, virtual_drivers[at]))
                        return DISCOVERY_KIND_VIRTUAL;
        for (positive at = 0; at < array_count(virtual_names); at++)
                if (discovery_starts(name, virtual_names[at]))
                        return DISCOVERY_KIND_VIRTUAL;
        if (type == 1)
                return DISCOVERY_KIND_ETHERNET;
        return DISCOVERY_KIND_OTHER;
}

//      A scope's word, by net.c's NET_SCOPE_*.
static const string_address discovery_scope_names[] = {
    "global", "private", "shared", "link-local", "unique local", "loopback",
    "other"};

//      An IPv4 address is held as IPv6 holds it, ::ffff:a.b.c.d.
static p8 discovery_mapped(const p8 address_to address)
{
        static const p8 prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

        return !memory_compare(address, prefix, 12);
}

/*      An address's scope: an IPv4 address's by net.c's rules, the one copy
        of them (net_scope_v4), and an IPv6 address's by its own blocks. */
static positive discovery_address_scope(const p8 address_to address)
{
        static const p8 loopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 1};

        if (discovery_mapped(address))
                return net_scope_v4(network_load_32((p8 address_to)address + 12));
        if (!memory_compare(address, loopback6, 16))
                return NET_SCOPE_LOOPBACK;
        if (address[0] == 0xfe && (address[1] & 0xc0) == 0x80)
                return NET_SCOPE_LINK;
        if ((address[0] & 0xfe) == 0xfc)
                return NET_SCOPE_UNIQUE;
        if ((address[0] & 0xe0) == 0x20)
                return NET_SCOPE_GLOBAL;
        return NET_SCOPE_OTHER;
}

/*      An address as text: dotted for IPv4, RFC 5952 for IPv6 (the longest
        run of two or more zero words, the first of equals, as ::). At most
        40 bytes with the end. */
static positive discovery_address_text(const p8 address_to address,
                                        p8 address_to text)
{
        p16 words[8];
        positive best = 8, best_count = 0, used = 0;

        if (discovery_mapped(address))
        {
                for (positive at = 0; at < 4; at++)
                {
                        if (at)
                                text[used++] = '.';
                        used += positive_into(text + used, address[12 + at]);
                }
                text[used] = end;
                return used;
        }
        for (positive at = 0; at < 8; at++)
                words[at] = (p16)((address[at * 2] << 8) | address[at * 2 + 1]);
        for (positive at = 0; at < 8;)
        {
                positive first = at;

                if (words[at])
                {
                        at++;
                        continue;
                }
                while (at < 8 && !words[at])
                        at++;
                if (at - first >= 2 && at - first > best_count)
                {
                        best = first;
                        best_count = at - first;
                }
        }
        for (positive at = 0; at < 8; at++)
        {
                if (at == best)
                {
                        text[used++] = ':';
                        if (!at)
                                text[used++] = ':';
                        at += best_count - 1;
                        continue;
                }
                used += positive_into_base(text + used, words[at], 16, false);
                if (at < 7)
                        text[used++] = ':';
        }
        text[used] = end;
        return used;
}

/*      An interface's name as a line says it: in quotes, each byte that is
        not printable ASCII, and a quote, a backslash or a semicolon, as
        \xHH. The kernel takes any byte in a name but a slash, a colon, a
        blank and the end, and Settings finds a row's value by its key at
        the start of a line or after a blank or a semicolon (settings.c's
        settings_find): a name said bare, "learned" or "x;nat", would carry
        another row's key and a value of its own choosing. Quoted and
        escaped it holds no blank and no semicolon, and its first byte
        follows a quote. At most DISCOVERY_NAME_TEXT bytes with the end. */
#define DISCOVERY_NAME_TEXT 68

static positive discovery_name_text(const p8 address_to name, p8 address_to text)
{
        static const p8 hex[] = "0123456789abcdef";
        positive used = 0;

        text[used++] = '"';
        for (positive at = 0; name[at] && at < 16; at++)
        {
                p8 c = name[at];

                if (c > ' ' && c < 0x7f && c != '"' && c != '\\' && c != ';')
                {
                        text[used++] = c;
                        continue;
                }
                text[used++] = '\\';
                text[used++] = 'x';
                text[used++] = hex[c >> 4];
                text[used++] = hex[c & 15];
        }
        text[used++] = '"';
        text[used] = end;
        return used;
}

//      A dotted IPv4 address of exactly length bytes, into address as
//      ::ffff:a.b.c.d.
static p8 discovery_parse4(const p8 address_to text, positive length,
                            p8 address_to address)
{
        positive part = 0, value = 0, digits = 0;

        for (positive at = 0; at <= length; at++)
        {
                if (at < length && text[at] >= '0' && text[at] <= '9')
                {
                        value = value * 10 + (positive)(text[at] - '0');
                        if (++digits > 3 || value > 255)
                                return false;
                        continue;
                }
                if (!digits || part > 3 || (at < length && text[at] != '.'))
                        return false;
                address[12 + part++] = (p8)value;
                value = digits = 0;
        }
        if (part != 4)
                return false;
        memory_zero(address, 10);
        address[10] = address[11] = 0xff;
        return true;
}

//      An IPv6 address in RFC 4291's text, of exactly length bytes; no zone
//      and no dotted tail (a trace never says either).
static p8 discovery_parse6(const p8 address_to text, positive length,
                            p8 address_to address)
{
        p16 words[8];
        positive count = 0, gap = 9, at = 0; // 9: no ::

        if (length >= 2 && text[0] == ':' && text[1] == ':')
        {
                gap = 0;
                at = 2;
        }
        else if (length && text[0] == ':')
                return false;
        while (at < length)
        {
                positive value = 0, digits = 0;

                while (at < length && digits < 5)
                {
                        p8 c = text[at];
                        positive nibble = c >= '0' && c <= '9'   ? (positive)(c - '0')
                                          : c >= 'a' && c <= 'f' ? (positive)(c - 'a' + 10)
                                          : c >= 'A' && c <= 'F' ? (positive)(c - 'A' + 10)
                                                                 : 16;
                        if (nibble == 16)
                                break;
                        value = value << 4 | nibble;
                        digits++;
                        at++;
                }
                if (!digits || digits > 4 || count == 8)
                        return false;
                words[count++] = (p16)value;
                if (at == length)
                        break;
                if (text[at] != ':')
                        return false;
                at++;
                if (at < length && text[at] == ':')
                {
                        if (gap != 9)
                                return false;
                        gap = count;
                        at++;
                }
                else if (at == length)
                        return false;
        }
        if (gap == 9 ? count != 8 : count > 7)
                return false;
        memory_zero(address, 16);
        for (positive word = 0; word < count; word++)
        {
                positive place = word < gap ? word : 8 - (count - word);

                address[place * 2] = (p8)(words[word] >> 8);
                address[place * 2 + 1] = (p8)words[word];
        }
        return true;
}

static p8 discovery_parse_address(const p8 address_to text, positive length,
                                   p8 address_to address)
{
        return discovery_parse4(text, length, address) ||
               discovery_parse6(text, length, address);
}

/*      STUN, RFC 8489. A Binding request is the 20-byte header (type 0x0001,
        the length of what follows, the magic cookie, 12 random bytes of
        transaction) and a FINGERPRINT: the CRC-32 of everything before it,
        xored with 0x5354554e. */
#define DISCOVERY_STUN_MAGIC 0x2112a442u
#define DISCOVERY_STUN_REQUEST 28
#define DISCOVERY_STUN_XOR_MAPPED 0x0020
#define DISCOVERY_STUN_MAPPED 0x0001
#define DISCOVERY_STUN_FINGERPRINT 0x8028

static p32 discovery_stun_fingerprint(const p8 address_to message,
                                       positive length)
{
        return ~hash_crc32(0xffffffffu, (address_any)message, length) ^
               0x5354554eu;
}

static fn discovery_put32(p8 address_to into, p32 value)
{
        into[0] = (p8)(value >> 24);
        into[1] = (p8)(value >> 16);
        into[2] = (p8)(value >> 8);
        into[3] = (p8)value;
}

static p32 discovery_get32(const p8 address_to from)
{
        return (p32)from[0] << 24 | (p32)from[1] << 16 | (p32)from[2] << 8 |
               from[3];
}

static p16 discovery_get16(const p8 address_to from)
{
        return (p16)(from[0] << 8 | from[1]);
}

static positive discovery_stun_request(p8 address_to into,
                                        const p8 address_to transaction)
{
        into[0] = 0x00;
        into[1] = 0x01;
        into[2] = 0x00;
        into[3] = 8;
        discovery_put32(into + 4, DISCOVERY_STUN_MAGIC);
        memory_copy(into + 8, transaction, 12);
        into[20] = 0x80;
        into[21] = 0x28;
        into[22] = 0x00;
        into[23] = 4;
        discovery_put32(into + 24, discovery_stun_fingerprint(into, 20));
        return DISCOVERY_STUN_REQUEST;
}

/*      A Binding success answer to transaction, and the address it says the
        request came from: XOR-MAPPED-ADDRESS, or the plain MAPPED-ADDRESS
        of an RFC 3489 server when there is no other. Everything is bounded
        by the datagram: the header's length must be what came, each
        attribute must fit, and a FINGERPRINT, if there, must be the last
        and must match. */
static p8 discovery_stun_read(const p8 address_to message, positive length,
                               const p8 address_to transaction,
                               p8 address_to address, p16 address_to port)
{
        positive at = 20;
        p8 found = 0; // 2 xor-mapped, 1 mapped

        if (length < 20 || length > 1500 || message[0] != 0x01 ||
            message[1] != 0x01 || (positive)discovery_get16(message + 2) + 20 != length ||
            (length & 3) || discovery_get32(message + 4) != DISCOVERY_STUN_MAGIC ||
            memory_compare(message + 8, transaction, 12))
                return false;
        while (at + 4 <= length)
        {
                p16 type = discovery_get16(message + at);
                positive size = discovery_get16(message + at + 2);
                const p8 address_to value = message + at + 4;
                positive next = at + 4 + ((size + 3) & ~(positive)3);

                if (next > length)
                        return false;
                if (type == DISCOVERY_STUN_FINGERPRINT)
                {
                        if (size != 4 || next != length ||
                            discovery_get32(value) !=
                                discovery_stun_fingerprint(message, at))
                                return false;
                }
                else if ((type == DISCOVERY_STUN_XOR_MAPPED ||
                          (type == DISCOVERY_STUN_MAPPED && found < 2)) &&
                         size >= 8 && value[0] == 0)
                {
                        p8 xored = type == DISCOVERY_STUN_XOR_MAPPED;
                        p8 family = value[1];
                        p16 got = discovery_get16(value + 2);

                        if (family == 1 && size == 8)
                        {
                                memory_zero(address, 10);
                                address[10] = address[11] = 0xff;
                                memory_copy(address + 12, value + 4, 4);
                                if (xored)
                                        for (positive byte = 0; byte < 4; byte++)
                                                address[12 + byte] ^= message[4 + byte];
                        }
                        else if (family == 2 && size == 20)
                        {
                                memory_copy(address, value + 4, 16);
                                if (xored)
                                        for (positive byte = 0; byte < 16; byte++)
                                                address[byte] ^= message[4 + byte];
                        }
                        else
                                return false;
                        address_to port = xored ? (p16)(got ^ 0x2112) : got;
                        found = xored ? 2 : 1;
                }
                at = next;
        }
        return at == length && found;
}

/*      The address a cdn-cgi/trace says the fetch came from: the line
        ip=ADDRESS, the rest of the trace ignored. */
static p8 discovery_trace_address(const p8 address_to body, positive length,
                                   p8 address_to address)
{
        for (positive at = 0; at < length;)
        {
                positive stop = at;

                while (stop < length && body[stop] != '\n')
                        stop++;
                if (stop - at > 3 && !memory_compare(body + at, "ip=", 3))
                {
                        positive size = stop - at - 3;

                        if (size && body[at + 3 + size - 1] == '\r')
                                size--;
                        return size && size < 46 &&
                               discovery_parse_address(body + at + 3, size, address);
                }
                at = stop + 1;
        }
        return false;
}

#define DISCOVERY_NAT_UNKNOWN 0     // no server answered
#define DISCOVERY_NAT_ONE 1         // one did: the mapping cannot be told
#define DISCOVERY_NAT_NONE 2        // the address seen is this machine's own
#define DISCOVERY_NAT_INDEPENDENT 3 // one mapping for both servers
#define DISCOVERY_NAT_DEPENDENT 4   // a mapping for each
//      What each is, as the value Settings shows, and how it was told.
static const string_address discovery_nat_names[] = {
    "not known", "not known", "none", "endpoint-independent", "endpoint-dependent"};
static const string_address discovery_nat_hows[] = {
    "no STUN server answered",
    "one STUN server answered, and two are needed to tell",
    "the public address is this machine's own",
    "one mapping for every server, so a hole can be punched",
    "a mapping for each server, so a hole cannot be punched without the "
    "router's help or a relay"};

/*      What the NAT does, from what each server saw (answered says which
        did) and whether the first mapping, the first server's or the
        second's when only it answered, is an address and port of this
        machine's own. */
static positive discovery_nat_classify(p8 answered, const p8 address_to first,
                                        p16 first_port, const p8 address_to second,
                                        p16 second_port, p8 own)
{
        if (!(answered & 3))
                return DISCOVERY_NAT_UNKNOWN;
        if (own)
                return DISCOVERY_NAT_NONE;
        if ((answered & 3) != 3)
                return DISCOVERY_NAT_ONE;
        return first_port == second_port && !memory_compare(first, second, 16)
                   ? DISCOVERY_NAT_INDEPENDENT
                   : DISCOVERY_NAT_DEPENDENT;
}

/* ---- what is here: the interfaces and their addresses ---- */

#define DISCOVERY_INTERFACES 16
#define DISCOVERY_ADDRESSES 64

typedef struct
{
        b32 index;
        p8 kind;
        p8 name[16];
} discovery_interface;

typedef struct
{
        b32 index;
        p8 prefix;
        p8 scope;
        p8 address[16];
} discovery_local;

typedef struct
{
        discovery_interface interface[DISCOVERY_INTERFACES];
        positive interfaces;
        discovery_local own[DISCOVERY_ADDRESSES];
        positive owns;
} discovery_here;

static discovery_interface address_to discovery_interface_of(
    discovery_here address_to here, b32 index)
{
        for (positive at = 0; at < here->interfaces; at++)
                if (here->interface[at].index == index)
                        return here->interface + at;
        return null;
}

#if !defined(MACOS)
static p8 discovery_link_seen(netlink_header address_to header,
                               address_any context)
{
        discovery_here address_to here = context;
        netlink_link address_to link = null;
        string_address name;
        p8 driver[16] = {0};
        positive size = 0;
        p8 address_to kind;
        discovery_interface address_to into;

        if (header->type != RTM_NEWLINK ||
            here->interfaces >= DISCOVERY_INTERFACES)
                return true;
        name = netlink_link_name(header, address_of link);
        if (!name || !link || string_length(name) >= 16)
                return true;
        kind = netlink_link_driver(header, address_of size);
        if (kind && size && size < sizeof driver)
                memory_copy(driver, kind, size);
        into = here->interface + here->interfaces++;
        into->index = (b32)link->index;
        string_copy_bounded((string_address)into->name, name, sizeof into->name);
        into->kind = (p8)discovery_interface_kind(
            name, link->kind, netlink_link_is_wireless(header, name),
            driver[0] ? (string_address)driver : null);
        return true;
}

static p8 discovery_address_seen(netlink_header address_to header,
                                  address_any context)
{
        discovery_here address_to here = context;
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to host;
        discovery_local address_to into;

        if (header->type != RTM_NEWADDR || !body ||
            here->owns >= DISCOVERY_ADDRESSES)
                return true;
        host = netlink_find(header, sizeof(netlink_address), IFA_LOCAL,
                            address_of size);
        if (!host)
                host = netlink_find(header, sizeof(netlink_address), IFA_ADDRESS,
                                    address_of size);
        if (!host)
                return true;
        into = here->own + here->owns;
        if (body->family == AF_INET && size == 4)
        {
                memory_zero(into->address, 10);
                into->address[10] = into->address[11] = 0xff;
                memory_copy(into->address + 12, host, 4);
        }
        else if (body->family == AF_INET6 && size == 16)
                memory_copy(into->address, host, 16);
        else
                return true;
        into->index = (b32)body->index;
        into->prefix = body->prefix;
        into->scope = (p8)discovery_address_scope(into->address);
        here->owns++;
        return true;
}
#endif

/*      What the kernel has, loopback's too, and nothing leaves the machine
        for it: every interface when links is asked, and every address of
        family (AF_UNSPEC for both). Each dump is net.c's, asked again while
        the kernel says its table changed under it (netlink_dump_again), as
        the lease's own walks are. */
static fn discovery_here_read_some(discovery_here address_to here, p8 links,
                                   p8 family)
{
        memory_zero(here, sizeof(address_to here));
#if !defined(MACOS)
        discovery_here first;
        bipolar handle = netlink_open_groups(0);

        if (handle < 0)
                return;
        memory_zero(address_of first, sizeof first);
        if (links)
                (void)netlink_dump_again((b32)handle, RTM_GETLINK,
                                         sizeof(netlink_link), AF_UNSPEC,
                                         discovery_link_seen, here,
                                         address_of first, sizeof first);
        first = address_to here;
        (void)netlink_dump_again((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                                 family, discovery_address_seen, here,
                                 address_of first, sizeof first);
        system_close((b32)handle);
#else
        (void)links;
        (void)family;
#endif
}

//      Every interface and every address the kernel has.
static fn discovery_here_read(discovery_here address_to here)
{
        discovery_here_read_some(here, true, AF_UNSPEC);
}

/* ---- the settings: the person's switches and lists ---- */

/*      Each lookup's switch is a word in /root (DISCOVERY_KEPT), on when
        there is none, and discovery's own is every lookup at once: off
        there is every lookup off, whatever its own word says. Turning one
        on is a deliberate act (registry.c's deliberate: a key or a press of
        a device plugged into the machine), turning one off never is. A word
        `moonwater link discovery` kept, before the switches were the
        network's, is moved to its new name the first time it is read.

        Beside them, what the lookups ask and how long they wait, each a
        file of its own and today's behaviour when there is none: the STUN
        servers (`network discovery stun servers`, up to four, the first two
        that resolve asked), the trace's URL (`network discovery https
        url`), each lookup's time limit, whether the proxy the environment
        names is taken (`network proxy`) and how often a quiet path is kept
        open (`network keepalive`, read by waterlink's listener).
        `moonwater network` writes them (src/sh/host/network.c, and the
        plain rows of registry.c for the numbers and the proxy). */
#define DISCOVERY_KEPT "/root"
#define DISCOVERY_PATH_ROOM 96

static const struct
{
        string_address word;
        string_address leaf;
        string_address was; // where `link discovery` kept it, or none
        positive ask;
        string_address says;
} discovery_switches[] = {
    {"discovery", "network.discovery", "link.discovery", 0, "every lookup below"},
    {"stun", "network.discovery.stun", "link.discovery.stun", DISCOVERY_ASK_STUN,
     "Public address discovery (STUN): a binding to the first two STUN servers "
     "listed that resolve"},
    {"https", "network.discovery.https", "link.discovery.https", DISCOVERY_ASK_HTTPS,
     "Public address discovery (HTTPS): the trace at the URL kept, where UDP "
     "does not get out"},
    {"announce", "network.discovery.announce", null, DISCOVERY_ASK_ANNOUNCE,
     "LAN announce (mDNS): the services of this machine announced on the local "
     "network, and questions about them answered"},
    {"listen", "network.discovery.listen", null, DISCOVERY_ASK_LISTEN,
     "LAN listen (mDNS): the local network asked for machines, what they "
     "announce heard, and a pairing code's sweep of the subnet"},
};

#define DISCOVERY_STUN_SERVERS 4
#define DISCOVERY_NAME_ROOM 64
#define DISCOVERY_URL_ROOM 128
#define DISCOVERY_LIST_ROOM 320
#define DISCOVERY_STUN_LEAF "network.discovery.stun.servers"
#define DISCOVERY_STUN_DEFAULT "stun.cloudflare.com:3478 stun.l.google.com:19302"
#define DISCOVERY_URL_LEAF "network.discovery.https.url"
#define DISCOVERY_URL_DEFAULT "https://cloudflare.com/cdn-cgi/trace"
//      The numbers and the proxy are plain rows of registry.c, whose files
//      and bounds these are: a word that is no number in them is the default.
#define DISCOVERY_STUN_MS_LEAF "network.discovery.stun.timeout"
#define DISCOVERY_STUN_MS_DEFAULT 1000
#define DISCOVERY_STUN_MS_LOW 200
#define DISCOVERY_STUN_MS_HIGH 10000
#define DISCOVERY_HTTPS_SECONDS_LEAF "network.discovery.https.timeout"
#define DISCOVERY_HTTPS_SECONDS_DEFAULT 3
#define DISCOVERY_HTTPS_SECONDS_LOW 1
#define DISCOVERY_HTTPS_SECONDS_HIGH 30
#define DISCOVERY_PROXY_LEAF "network.proxy"
#define DISCOVERY_KEEPALIVE_LEAF "network.keepalive"
#define DISCOVERY_KEEPALIVE_DEFAULT 10
#define DISCOVERY_KEEPALIVE_LOW 1
#define DISCOVERY_KEEPALIVE_HIGH 40 // under the 45 seconds a session is given up after

typedef struct
{
        p8 name[DISCOVERY_NAME_ROOM];
        p16 port;
} discovery_stun_server;

typedef struct
{
        p8 switched[array_count(discovery_switches)]; // each switch's own word
        positive ask; // DISCOVERY_ASK_*: what they allow together, nothing while discovery is off
        discovery_stun_server server[DISCOVERY_STUN_SERVERS];
        positive servers;
        p8 servers_kept; // the list is one kept, not the default
        p8 url[DISCOVERY_URL_ROOM];
        p8 url_kept;
        positive stun_ms;       // a STUN binding's wait, all its servers at once
        positive https_seconds; // the trace's, the whole fetch
        p8 proxy;               // the proxy the environment names is taken
} discovery_settings;

//      The file a setting is kept in: the directory, a slash and its leaf.
static fn discovery_kept_path(string_address directory, string_address leaf,
                              p8 address_to path)
{
        string_copy_bounded((string_address)path, directory, DISCOVERY_PATH_ROOM);
        string_append_bounded((string_address)path, "/", DISCOVERY_PATH_ROOM);
        string_append_bounded((string_address)path, leaf, DISCOVERY_PATH_ROOM);
}

/*      One STUN server as it is written, NAME or NAME:PORT, of exactly
        length bytes: a name of letters, digits, dots and dashes that starts
        with a letter or a digit (an IPv4 address is one), at most 63 bytes,
        kept in small letters; a port from 1 to 65535, 3478 when none is
        said. */
static p8 discovery_server_parse(const p8 address_to text, positive length,
                                 discovery_stun_server address_to server)
{
        positive name = 0, port = 0, digits = 0;

        while (name < length && text[name] != ':')
                name++;
        if (!name || name >= DISCOVERY_NAME_ROOM || text[0] == '.' || text[0] == '-')
                return false;
        for (positive at = 0; at < name; at++)
        {
                p8 c = text[at];

                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '-'))
                        return false;
                server->name[at] = c >= 'A' && c <= 'Z' ? (p8)(c + 32) : c;
        }
        server->name[name] = end;
        for (positive at = name + 1; at < length; at++)
        {
                if (text[at] < '0' || text[at] > '9' || ++digits > 5)
                        return false;
                port = port * 10 + (positive)(text[at] - '0');
        }
        if (name < length && (!digits || !port || port > 65535))
                return false;
        server->port = (p16)(name < length ? port : DISCOVERY_STUN_PORT);
        return true;
}

/*      A list of servers, words a blank apart: one to DISCOVERY_STUN_SERVERS
        of them, each good and each named once, or false and no list. */
static p8 discovery_servers_parse(const p8 address_to text, positive length,
                                  discovery_stun_server address_to servers,
                                  positive address_to count)
{
        positive at = 0, made = 0;

        address_to count = 0;
        while (at < length)
        {
                positive size = 0;

                while (at < length && (text[at] == ' ' || text[at] == '\t'))
                        at++;
                while (at + size < length && text[at + size] != ' ' &&
                       text[at + size] != '\t')
                        size++;
                if (!size)
                        break;
                if (made == DISCOVERY_STUN_SERVERS ||
                    !discovery_server_parse(text + at, size, servers + made))
                        return false;
                for (positive before = 0; before < made; before++)
                        if (servers[before].port == servers[made].port &&
                            string_equals((string_address)servers[before].name,
                                          (string_address)servers[made].name))
                                return false;
                made++;
                at += size;
        }
        address_to count = made;
        return made != 0;
}

/*      A trace's URL: https://, a host, and what follows it, every byte a
        printable one but a blank, a quote, a backslash or a semicolon (a
        line of the report says it, and Settings reads a line by its words),
        shorter than DISCOVERY_URL_ROOM. */
static p8 discovery_url_good(const p8 address_to text, positive length)
{
        if (length >= DISCOVERY_URL_ROOM || length <= 8 ||
            memory_compare(text, "https://", 8) || text[8] == '/' ||
            text[8] == ':')
                return false;
        for (positive at = 0; at < length; at++)
                if (text[at] <= ' ' || text[at] >= 0x7f || text[at] == '"' ||
                    text[at] == '\\' || text[at] == ';')
                        return false;
        return true;
}

//      The host a good URL names, into a room of DISCOVERY_NAME_ROOM bytes.
static fn discovery_url_host(const p8 address_to url, p8 address_to host)
{
        positive length = 0;

        while (url[8 + length] && url[8 + length] != '/' && url[8 + length] != ':' &&
               url[8 + length] != '?' && url[8 + length] != '#' &&
               length + 1 < DISCOVERY_NAME_ROOM)
        {
                host[length] = url[8 + length];
                length++;
        }
        host[length] = end;
}

/*      A whole number a plain row kept, from low to high; the default for
        none, for default, and for anything else. */
static positive discovery_number_of(const p8 address_to word, positive low,
                                    positive high, positive standard)
{
        positive value = 0, at = 0;

        while (word[at] >= '0' && word[at] <= '9' && at < 9)
                value = value * 10 + (positive)(word[at++] - '0');
        return at && !word[at] && value >= low && value <= high ? value : standard;
}

static positive discovery_kept_number(string_address directory, string_address leaf,
                                      positive low, positive high, positive standard)
{
        p8 path[DISCOVERY_PATH_ROOM];
        p8 word[16];

        discovery_kept_path(directory, leaf, path);
        if (host_read_word((string_address)path, word, sizeof word) <= 0)
                return standard;
        return discovery_number_of(word, low, high, standard);
}

//      A switch of a word, on when there is none, as the lookups' are.
static p8 discovery_kept_on(string_address directory, string_address leaf)
{
        p8 path[DISCOVERY_PATH_ROOM];
        p8 word[8];

        discovery_kept_path(directory, leaf, path);
        return host_read_word((string_address)path, word, sizeof word) < 0 ||
               !string_equals((string_address)word, "off");
}

//      What a file of one line holds, whole, or -1 when it is not there or
//      does not fit: a cut line is no line.
static bipolar discovery_kept_line(string_address directory, string_address leaf,
                                   p8 address_to text, positive room)
{
        p8 path[DISCOVERY_PATH_ROOM];
        bipolar got;

        discovery_kept_path(directory, leaf, path);
        got = host_read_word((string_address)path, text, room);
        return got >= (bipolar)room - 1 ? -1 : got;
}

static fn discovery_settings_read_at(string_address directory,
                                     discovery_settings address_to settings)
{
        p8 text[DISCOVERY_LIST_ROOM];
        bipolar got;

        memory_zero(settings, sizeof(address_to settings));
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
#if !defined(MACOS)
                if (discovery_switches[at].was)
                {
                        p8 path[DISCOVERY_PATH_ROOM];
                        p8 was[DISCOVERY_PATH_ROOM];

                        //      Never over a word the network's verb has kept since.
                        discovery_kept_path(directory, discovery_switches[at].leaf, path);
                        discovery_kept_path(directory, discovery_switches[at].was, was);
                        (void)system_rename_at(AT_FDCWD, (string_address)was, AT_FDCWD,
                                               (string_address)path,
                                               1 /* RENAME_NOREPLACE */);
                }
#endif
                settings->switched[at] = discovery_kept_on(directory, discovery_switches[at].leaf);
        }
        for (positive at = 1; settings->switched[0] && at < array_count(discovery_switches); at++)
                if (settings->switched[at])
                        settings->ask |= discovery_switches[at].ask;

        got = discovery_kept_line(directory, DISCOVERY_STUN_LEAF, text, sizeof text);
        settings->servers_kept =
            got > 0 && discovery_servers_parse(text, (positive)got, settings->server,
                                               address_of settings->servers);
        if (!settings->servers_kept)
                (void)discovery_servers_parse((const p8 address_to)DISCOVERY_STUN_DEFAULT,
                                              sizeof(DISCOVERY_STUN_DEFAULT) - 1,
                                              settings->server, address_of settings->servers);
        got = discovery_kept_line(directory, DISCOVERY_URL_LEAF, text, sizeof text);
        settings->url_kept = got > 0 && discovery_url_good(text, (positive)got);
        string_copy_bounded((string_address)settings->url,
                            settings->url_kept ? (string_address)text
                                               : (string_address)DISCOVERY_URL_DEFAULT,
                            sizeof settings->url);
        settings->stun_ms = discovery_kept_number(directory, DISCOVERY_STUN_MS_LEAF,
                                                  DISCOVERY_STUN_MS_LOW,
                                                  DISCOVERY_STUN_MS_HIGH,
                                                  DISCOVERY_STUN_MS_DEFAULT);
        settings->https_seconds = discovery_kept_number(
            directory, DISCOVERY_HTTPS_SECONDS_LEAF, DISCOVERY_HTTPS_SECONDS_LOW,
            DISCOVERY_HTTPS_SECONDS_HIGH, DISCOVERY_HTTPS_SECONDS_DEFAULT);
        settings->proxy = discovery_kept_on(directory, DISCOVERY_PROXY_LEAF);
}

static fn discovery_settings_read(discovery_settings address_to settings)
{
        discovery_settings_read_at(DISCOVERY_KEPT, settings);
}

/*      How long a quiet path waits before a keepalive holds its NAT's
        mapping open, in seconds: waterlink's listener asks it as it starts. */
static positive discovery_keepalive_seconds(void)
{
        return discovery_kept_number(DISCOVERY_KEPT, DISCOVERY_KEEPALIVE_LEAF,
                                     DISCOVERY_KEEPALIVE_LOW, DISCOVERY_KEEPALIVE_HIGH,
                                     DISCOVERY_KEEPALIVE_DEFAULT);
}

/* ---- what a gather found, kept with its time ---- */

#define DISCOVERY_CACHE_MAGIC 0x6e6e6f63u // "conn"
#define DISCOVERY_UDP_NOT_ASKED 0
#define DISCOVERY_UDP_OUT 1
#define DISCOVERY_UDP_BLOCKED 2
#define DISCOVERY_UDP_NO_NAME 3
#define DISCOVERY_HTTPS_NOT_ASKED 0
#define DISCOVERY_HTTPS_ANSWERED 1
#define DISCOVERY_HTTPS_FAILED 2

typedef struct
{
        p32 magic;
        p32 size;
        p64 asked;      // wall seconds of the gather, 0 for never
        p64 stun_when;  // of the last STUN answer
        p64 https_when; // of the last trace answer
        p8 udp;         // DISCOVERY_UDP_*
        p8 nat;         // DISCOVERY_NAT_*
        p8 answered;    // which STUN servers answered: 1 the first, 2 the second
        p8 https;       // DISCOVERY_HTTPS_*
        b32 https_status; // the HTTP client's status when it failed
        p8 mapped[2][16];
        p16 mapped_port[2];
        p16 local_port;
        p16 reserved;
        p8 trace[16];
        discovery_stun_server asked_of[2]; // the server each of the two was
        p8 trace_host[DISCOVERY_NAME_ROOM]; // the host the trace was asked of
} discovery_found;

//      The wall clock's second, asked of the kernel itself where there is
//      no shell around this file to ask for it.
static p64 discovery_now(void)
{
#if defined(MACOS)
        return system_clock_ns(0) / 1000000000ull;
#else
        p64 moment[2] = {0, 0};

        return system_call_2(syscall(clock_gettime), 0, (positive)moment) < 0
                   ? 0
                   : moment[0];
#endif
}

#if !defined(MACOS)
//      A request to each server named that has not answered yet.
static fn discovery_stun_send(bipolar handle, discovery_found address_to found,
                               const p32 address_to server, p8 named,
                               p8 transaction[2][12])
{
        p8 request[DISCOVERY_STUN_REQUEST];

        for (positive at = 0; at < 2; at++)
        {
                socket_address_internet to = {
                    .family = AF_INET,
                    .port = network_order_16(found->asked_of[at].port),
                    .host = network_order_32(server[at])};

                if (!(named & (1u << at)) || (found->answered & (1u << at)))
                        continue;
                (void)discovery_stun_request(request, transaction[at]);
                discovery_sent++;
                (void)socket_send((b32)handle, request, sizeof request, 0,
                                  address_of to, sizeof to);
        }
}

//      The answers that come until the monotonic clock reads until.
static fn discovery_stun_wait(bipolar handle, discovery_found address_to found,
                               p8 named, p8 transaction[2][12], p64 until)
{
        for (;;)
        {
                p64 now = clock_monotonic_nanoseconds();
                timespec limit;
                p8 answer[1500];
                bipolar got;

                if (now >= until || (found->answered & named) == named)
                        return;
                limit.tv_sec = (b64)((until - now) / 1000000000ull);
                limit.tv_nsec = (b64)((until - now) % 1000000000ull);
                if (descriptor_wait_readable(handle, address_of limit, null) <= 0)
                        continue;
                got = socket_receive((b32)handle, answer, sizeof answer, MSG_DONTWAIT,
                                     null, null);
                for (positive at = 0; got > 0 && at < 2; at++)
                {
                        p8 address[16];
                        p16 port = 0;

                        if (!(found->answered & (1u << at)) &&
                            discovery_stun_read(answer, (positive)got, transaction[at],
                                                 address, address_of port))
                        {
                                memory_copy(found->mapped[at], address, 16);
                                found->mapped_port[at] = port;
                                found->answered |= (p8)(1u << at);
                        }
                }
        }
}

/*      One socket, a request to each of the first two servers listed whose
        names resolve and one more halfway through the time limit to any
        that has not answered, and the limit in all (a second unless the
        person set another). The same socket for both is what lets the two
        mappings be compared. */
static fn discovery_stun(discovery_found address_to found,
                          const discovery_here address_to here,
                          const discovery_settings address_to settings)
{
        p32 server[2] = {0, 0};
        p8 transaction[2][12];
        p8 named = 0;
        positive slot = 0;
        positive seconds = (settings->stun_ms + 999) / 1000;
        bipolar handle;
        socket_address_internet self = {0};
        b32 self_size = sizeof self;
        p64 start;

        for (positive at = 0; at < settings->servers && slot < 2; at++)
                if (net_resolve_v4_apart((string_address)settings->server[at].name,
                                         server + slot, seconds) == DNS_OK)
                {
                        found->asked_of[slot] = settings->server[at];
                        named |= (p8)(1u << slot++);
                }
        if (!named)
        {
                found->udp = DISCOVERY_UDP_NO_NAME;
                return;
        }
        handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (handle < 0 || system_random_fill(transaction, sizeof transaction, 0) < 0)
        {
                if (handle >= 0)
                        socket_close((b32)handle);
                found->udp = DISCOVERY_UDP_BLOCKED;
                return;
        }
        start = clock_monotonic_nanoseconds();
        discovery_stun_send(handle, found, server, named, transaction);
        discovery_stun_wait(handle, found, named, transaction,
                             start + (p64)settings->stun_ms * 500000ull);
        if ((found->answered & named) != named)
        {
                discovery_stun_send(handle, found, server, named, transaction);
                discovery_stun_wait(handle, found, named, transaction,
                                     start + (p64)settings->stun_ms * 1000000ull);
        }
        if (socket_name((b32)handle, address_of self, address_of self_size) >= 0)
                found->local_port = network_order_16(self.port);
        socket_close((b32)handle);
        found->udp = found->answered ? DISCOVERY_UDP_OUT : DISCOVERY_UDP_BLOCKED;
        if (found->answered)
                found->stun_when = discovery_now();
        {
                p8 own = false;
                positive first = found->answered & 1 ? 0 : 1;

                for (positive at = 0; at < here->owns; at++)
                        own |= !memory_compare(here->own[at].address,
                                               found->mapped[first], 16);
                own &= found->mapped_port[first] == found->local_port;
                found->nat = (p8)discovery_nat_classify(
                    found->answered, found->mapped[first], found->mapped_port[first],
                    found->mapped[1], found->mapped_port[1], own);
        }
}

/*      The trace, apart: the TLS, the HTTP and the reading of the trace run
        in a confined child, and what comes back is a status and sixteen
        bytes, believed only as an address. Its time limit is the whole
        fetch (three seconds unless the person set another), each wait of
        its head and its body a second less. */
static const http_manners discovery_trace_manners = {
    (string_address)"Moonwater", '1', false, true, false, 2, 2, 4096, 0, false, 3};

typedef struct
{
        b64 status;
        p8 found;
        p8 address[16];
} discovery_trace_said;

static bool discovery_trace_task(address_any context, bipolar answer)
{
        const discovery_settings address_to settings = context;
        http_manners manners = discovery_trace_manners;
        discovery_trace_said said;
        http_buffer body = {0};

        manners.chain_seconds = settings->https_seconds;
        manners.head_seconds = manners.body_seconds =
            settings->https_seconds > 1 ? settings->https_seconds - 1 : 1;
        memory_zero(address_of said, sizeof said);
        said.status = http_run((string_address)settings->url, address_of manners,
                               true, -1, address_of body, null, null, null);
        said.found = !said.status &&
                     discovery_trace_address(body.bytes, body.used, said.address);
        byte_store_release(address_of body);
        return system_write_all((positive)answer, address_of said, sizeof said) ==
               sizeof said;
}

static fn discovery_https(discovery_found address_to found,
                          const discovery_settings address_to settings)
{
        byte_store got = {0};
        discovery_trace_said said;
        bipolar ran;

        discovery_sent++;
        ran = net_apart(discovery_trace_task, (address_any)settings, -1, address_of got,
                        sizeof said, false);
        found->https = DISCOVERY_HTTPS_FAILED;
        found->https_status = ran != NET_APART_OK ? (b32)ran : -1;
        if (ran == NET_APART_OK && got.used == sizeof said)
        {
                memory_copy(address_of said, got.bytes, sizeof said);
                found->https_status = (b32)said.status;
                if (!said.status && said.found == 1)
                {
                        found->https = DISCOVERY_HTTPS_ANSWERED;
                        found->https_status = 0;
                        memory_copy(found->trace, said.address, 16);
                        discovery_url_host(settings->url, found->trace_host);
                        found->https_when = discovery_now();
                }
        }
        byte_store_release(address_of got);
}
#endif

//      What was kept, or zeros when nothing whole was. The names in it are
//      ended where their room ends, whatever the file says.
static fn discovery_found_read(discovery_found address_to found)
{
        if (file_read_once_at(AT_FDCWD, DISCOVERY_CACHE_PATH, found,
                              sizeof(address_to found)) != (bipolar)sizeof(address_to found) ||
            found->magic != DISCOVERY_CACHE_MAGIC ||
            found->size != sizeof(address_to found) ||
            found->udp > DISCOVERY_UDP_NO_NAME ||
            found->nat > DISCOVERY_NAT_DEPENDENT ||
            found->https > DISCOVERY_HTTPS_FAILED)
                memory_zero(found, sizeof(address_to found));
        found->asked_of[0].name[DISCOVERY_NAME_ROOM - 1] = end;
        found->asked_of[1].name[DISCOVERY_NAME_ROOM - 1] = end;
        found->trace_host[DISCOVERY_NAME_ROOM - 1] = end;
}

/*      What a gather that learned nothing new keeps of the ones before: the
        last STUN answer whole (which servers answered, who they were, what
        each saw, the NAT told from it and when) and the last trace's
        address, its host and when, however many gathers ago they came. */
static fn discovery_found_keep(discovery_found address_to found,
                                const discovery_found address_to before)
{
        if (!found->answered && before->answered)
        {
                found->answered = before->answered;
                found->nat = before->nat;
                memory_copy(found->mapped, before->mapped, sizeof found->mapped);
                memory_copy(found->mapped_port, before->mapped_port,
                            sizeof found->mapped_port);
                memory_copy(found->asked_of, before->asked_of, sizeof found->asked_of);
                found->stun_when = before->stun_when;
        }
        if (found->https != DISCOVERY_HTTPS_ANSWERED && before->https_when)
        {
                memory_copy(found->trace, before->trace, 16);
                memory_copy(found->trace_host, before->trace_host,
                            sizeof found->trace_host);
                found->https_when = before->https_when;
        }
}
/*      Root's, 0600, never through a link planted at the name, and whole:
        written under a name of this process's own beside it and renamed
        over it, so a reader (Settings, another gather) never takes it cut,
        and a file there of another mode is replaced. A reader takes only a
        whole record with the magic and the size. */
static fn discovery_found_write(const discovery_found address_to found)
{
#if !defined(MACOS)
        p8 beside[64];
        positive used = string_length(DISCOVERY_CACHE_PATH);
        bipolar handle;
        p8 whole;

        memory_copy(beside, DISCOVERY_CACHE_PATH, used);
        beside[used++] = '.';
        used += positive_into(beside + used,
                              (positive)system_call(syscall(getpid)));
        beside[used] = end;
        (void)system_remove_at(AT_FDCWD, beside, 0);
        handle = system_open_output_at(AT_FDCWD, (string_address)beside, false,
                                       0600);
        if (handle < 0)
                return;
        whole = system_write_all((positive)handle, found,
                                 sizeof(address_to found)) ==
                sizeof(address_to found);
        system_close((b32)handle);
        if (!whole || system_rename_at(AT_FDCWD, beside, AT_FDCWD,
                                       DISCOVERY_CACHE_PATH, 0) < 0)
                (void)system_remove_at(AT_FDCWD, beside, 0);
#else
        bipolar handle = system_open_output_at(AT_FDCWD, DISCOVERY_CACHE_PATH,
                                               true, 0600);

        if (handle >= 0)
        {
                (void)system_write_all((positive)handle, found,
                                       sizeof(address_to found));
                system_close((b32)handle);
        }
#endif
}

/*      The lookups the settings allow, each within its time limit, into a
        record of this moment. Off is off: no lookup allowed is no packet. */
static fn discovery_look(const discovery_settings address_to settings,
                         discovery_found address_to found,
                         const discovery_here address_to here)
{
        memory_zero(found, sizeof(address_to found));
        found->magic = DISCOVERY_CACHE_MAGIC;
        found->size = sizeof(address_to found);
        found->asked = discovery_now();
#if !defined(MACOS)
        if (settings->ask & DISCOVERY_ASK_STUN)
                discovery_stun(found, here, settings);
        if ((settings->ask & DISCOVERY_ASK_HTTPS) && found->udp != DISCOVERY_UDP_OUT)
                discovery_https(found, settings);
#else
        (void)settings;
        (void)here;
#endif
}

/*      A gather: the interfaces read, the lookups made, and what they
        found kept beside what was found before (a lookup not made keeps its
        last answer and its time, so a switch turned off hides nothing it
        learned). */
static fn discovery_gather(const discovery_settings address_to settings,
                           discovery_found address_to found,
                           discovery_here address_to here)
{
        discovery_found before;

        discovery_here_read(here);
        discovery_found_read(address_of before);
        discovery_look(settings, found, here);
        discovery_found_keep(found, address_of before);
        discovery_found_write(found);
}

/*      Whether the network's change is owed a gather (src/sh/net.c's
        net_discovery_soon: the watcher's start, a lease that is new or
        moved, a link that came or went): only while a lookup may send, so
        with discovery off, or both its lookups off, nothing is started and
        nothing leaves. The gather is the one `network discovery refresh`
        makes. */
static p8 discovery_auto_wanted(const discovery_settings address_to settings)
{
        return (settings->ask & (DISCOVERY_ASK_STUN | DISCOVERY_ASK_HTTPS)) != 0;
}

/*      The proxy a program here is told to use, as host:port with any
        user and password left out, or none; none as well when the person
        said to ignore it (`network proxy off`). */
static p8 discovery_proxy(const discovery_settings address_to settings,
                          p8 address_to text, positive room)
{
        static const string_address names[] = {"HTTPS_PROXY", "https_proxy",
                                               "ALL_PROXY", "all_proxy"};
        string_address value = null;
        positive at = 0, length;

        text[0] = end;
        for (positive name = 0; settings->proxy && !value && name < array_count(names); name++)
        {
                value = getenv(names[name]);
                if (value && !value[0])
                        value = null;
        }
        if (!value)
                return false;
        for (positive scan = 0; value[scan]; scan++)
                if (value[scan] == ':' && value[scan + 1] == '/' &&
                    value[scan + 2] == '/')
                {
                        at = scan + 3;
                        break;
                }
        for (positive scan = at; value[scan] && value[scan] != '/'; scan++)
                if (value[scan] == '@')
                        at = scan + 1;
        for (length = 0; value[at + length] && value[at + length] != '/' &&
                         length + 1 < room;
             length++)
        {
                p8 c = (p8)value[at + length];

                //      What is printed is an address: anything else is a dot.
                text[length] = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                                       (c >= 'A' && c <= 'Z') || c == '.' ||
                                       c == ':' || c == '-' || c == '[' || c == ']'
                                   ? c
                                   : '.';
        }
        text[length] = end;
        return length != 0;
}

/* ---- what was found, said ---- */

//      How long ago a wall second was, in 32 bytes: "119s ago", "119m ago",
//      or hours.
static fn discovery_ago(p64 then, p8 address_to text)
{
        p64 wall = discovery_now();
        p64 gone = wall > then ? wall - then : 0;

        text[positive_into(text, (positive)(gone < 120    ? gone
                                            : gone < 7200 ? gone / 60
                                                          : gone / 3600))] = 0;
        string_append_bounded(text, gone < 120    ? "s ago"
                                    : gone < 7200 ? "m ago"
                                                  : "h ago",
                              32);
}

static fn discovery_when(p64 when, p8 address_to text)
{
        if (!when)
                string_copy((string_address)text, "never");
        else
                discovery_ago(when, text);
}

//      Why the trace was not had, in words.
static string_address discovery_https_reason(b32 status)
{
#if !defined(MACOS)
        switch (status)
        {
        case HTTP_NO_HOST:
                return (string_address) "its host did not resolve";
        case HTTP_NO_ROUTE:
                return (string_address) "no route to it";
        case HTTP_UNTRUSTED:
                return (string_address) "its certificate is not one Moonwater trusts: "
                                        "something on the way answers TLS for it";
        case HTTP_TLS:
        case HTTP_EXPIRED:
        case HTTP_NOT_YET:
        case HTTP_MISMATCH:
        case HTTP_UNCHECKED:
        case HTTP_CLOCK:
                return (string_address) "TLS failed -- is the clock right?";
        case HTTP_STATUS:
                return (string_address) "it answered with an error";
        case HTTP_MALFORMED:
                return (string_address) "the answer named no address";
        }
#else
        (void)status;
#endif
        return (string_address) "no answer";
}

//      A server as a line says it, NAME:PORT, into DISCOVERY_NAME_ROOM + 8.
static fn discovery_server_text(const discovery_stun_server address_to server,
                                p8 address_to text)
{
        positive used = string_length((string_address)server->name);

        memory_copy(text, server->name, used);
        text[used++] = ':';
        text[used + positive_into(text + used, (positive)server->port)] = end;
}

/*
        The page, prefix before each line (the doctor's indent, or nothing).
        Each line is a key and its value alone, the line a Settings row
        reads: a switch's word, a list, an address, a word of the tables.
        How a value was learned and when are lines of their own, the key
        and how or when, after it. A line's key comes before any other line
        holds those words after a blank or a semicolon, which a check holds
        for every Settings row of the verb; nothing in a line is another's
        text but an interface's name, said quoted and escaped
        (discovery_name_text), the servers and the URL, whose bytes their
        parsers allow. Every value is a number, one of the tables' words,
        or one of those.
*/
static fn discovery_report(writer say, const discovery_settings address_to settings,
                             const discovery_found address_to found,
                             discovery_here address_to here, string_address prefix)
{
        p8 text[48];
        p8 name[DISCOVERY_NAME_TEXT];
        p8 server[DISCOVERY_NAME_ROOM + 8];
        p8 ago[32];
        p8 when[32];
        p8 proxy[64];
        const p8 address_to v6 = null;
        discovery_interface address_to v6_where = null;
        positive first = found->answered & 1 ? 0 : 1;
        positive shown = 0;

        for (positive at = 0; at < array_count(discovery_switches); at++)
                string_format(say, "%s%s %s\n", prefix,
                              (string_address)discovery_switches[at].word,
                              settings->switched[at] ? "on" : "off");
        string_format(say, "%sstun servers", prefix);
        for (positive at = 0; at < settings->servers; at++)
        {
                discovery_server_text(settings->server + at, server);
                string_format(say, " %s", (string_address)server);
        }
        string_format(say, "\n%shttps url %s\n", prefix, (string_address)settings->url);

        if (found->answered)
        {
                discovery_address_text(found->mapped[first], text);
                discovery_when(found->stun_when, when);
                discovery_server_text(found->asked_of + first, server);
                string_format(say, "%spublic ipv4 %s\n"
                                   "%spublic ipv4 how STUN, port %p, as %s saw it\n"
                                   "%spublic ipv4 when %s\n",
                              prefix, (string_address)text, prefix,
                              (positive)found->mapped_port[first], (string_address)server,
                              prefix, (string_address)when);
        }
        else if (found->https_when && discovery_mapped(found->trace))
        {
                discovery_address_text(found->trace, text);
                discovery_when(found->https_when, when);
                string_format(say, "%spublic ipv4 %s\n"
                                   "%spublic ipv4 how HTTPS, the trace of %s\n"
                                   "%spublic ipv4 when %s\n",
                              prefix, (string_address)text, prefix,
                              (string_address)found->trace_host, prefix,
                              (string_address)when);
        }
        else
                string_format(say, "%spublic ipv4 not known\n", prefix);

        if (found->https_when && !discovery_mapped(found->trace))
                v6 = found->trace;
        for (positive at = 0; !v6 && at < here->owns; at++)
                if (!discovery_mapped(here->own[at].address) &&
                    here->own[at].scope == NET_SCOPE_GLOBAL)
                {
                        v6 = here->own[at].address;
                        v6_where = discovery_interface_of(here, here->own[at].index);
                }
        if (v6 && v6 != found->trace)
        {
                discovery_address_text(v6, text);
                if (v6_where)
                        discovery_name_text(v6_where->name, name);
                string_format(say, "%spublic ipv6 %s\n"
                                   "%spublic ipv6 how the global address of %s, "
                                   "which needs no NAT\n"
                                   "%spublic ipv6 when now\n",
                              prefix, (string_address)text, prefix,
                              v6_where ? (string_address)name : (string_address) "an interface",
                              prefix);
        }
        else if (v6)
        {
                discovery_address_text(v6, text);
                discovery_when(found->https_when, when);
                string_format(say, "%spublic ipv6 %s\n"
                                   "%spublic ipv6 how HTTPS, the trace of %s\n"
                                   "%spublic ipv6 when %s\n",
                              prefix, (string_address)text, prefix,
                              (string_address)found->trace_host, prefix,
                              (string_address)when);
        }
        else
                string_format(say, "%spublic ipv6 none\n"
                                   "%spublic ipv6 how no interface has a global IPv6 "
                                   "address\n", prefix, prefix);

        discovery_when(found->asked, ago);
        if (!found->asked)
                string_format(say, "%sudp out not known\n"
                                   "%sudp out how nothing looked yet "
                                   "(moonwater network discovery refresh)\n",
                              prefix, prefix);
        else if (found->udp == DISCOVERY_UDP_OUT)
                string_format(say, "%sudp out yes\n%sudp out how a STUN server answered\n",
                              prefix, prefix);
        else if (found->udp == DISCOVERY_UDP_BLOCKED)
                string_format(say, "%sudp out blocked\n"
                                   "%sudp out how no STUN server answered in %p ms\n",
                              prefix, prefix, settings->stun_ms);
        else if (found->udp == DISCOVERY_UDP_NO_NAME)
                string_format(say, "%sudp out not known\n"
                                   "%sudp out how the names of the STUN servers were "
                                   "not found\n", prefix, prefix);
        else
                string_format(say, "%sudp out not asked\n%sudp out how %s\n", prefix, prefix,
                              settings->switched[0] ? "Public address discovery (STUN) is off"
                                                    : "discovery is off");
        if (found->asked)
                string_format(say, "%sudp out when %s\n", prefix, (string_address)ago);

        if (found->udp == DISCOVERY_UDP_NOT_ASKED && !found->answered)
                string_format(say, "%snat not known\n%snat how STUN was not asked\n",
                              prefix, prefix);
        else
                string_format(say, "%snat %s\n%snat how %s\n", prefix,
                              (string_address)discovery_nat_names[found->nat], prefix,
                              (string_address)discovery_nat_hows[found->nat]);

        if (found->https == DISCOVERY_HTTPS_ANSWERED)
                string_format(say, "%strace answered\n%strace how %s answered\n",
                              prefix, prefix, (string_address)found->trace_host);
        else if (found->https == DISCOVERY_HTTPS_FAILED)
                string_format(say, "%strace failed\n%strace how %s (status %b)\n",
                              prefix, prefix, discovery_https_reason(found->https_status),
                              (bipolar)found->https_status);
        else if (found->udp == DISCOVERY_UDP_OUT)
                string_format(say, "%strace not asked\n%strace how UDP gets out\n",
                              prefix, prefix);
        else
                string_format(say, "%strace not asked\n", prefix);
        if (found->https != DISCOVERY_HTTPS_NOT_ASKED)
                string_format(say, "%strace when %s\n", prefix, (string_address)ago);

        if (discovery_proxy(settings, proxy, sizeof proxy))
                string_format(say, "%sproxy %s\n%sproxy how named by the environment; "
                                   "nothing goes through it yet\n",
                              prefix, (string_address)proxy, prefix);
        else if (!settings->proxy)
                string_format(say, "%sproxy ignored\n%sproxy how network proxy is off\n",
                              prefix, prefix);
        else
                string_format(say, "%sproxy none\n", prefix);

        string_format(say, "%saddresses", prefix);
        for (positive at = 0; at < here->owns; at++)
        {
                discovery_local address_to mine = here->own + at;
                discovery_interface address_to where =
                    discovery_interface_of(here, mine->index);

                if (mine->scope == NET_SCOPE_LOOPBACK)
                        continue;
                discovery_address_text(mine->address, text);
                if (where)
                        discovery_name_text(where->name, name);
                string_format(say, "%s %s %s %s/%p %s", shown++ ? ";" : "",
                              where ? (string_address)name : (string_address) "?",
                              (string_address)discovery_kind_names[where ? where->kind : 0],
                              (string_address)text, (positive)mine->prefix,
                              (string_address)discovery_scope_names[mine->scope]);
        }
        string_format(say, "%s\n", shown ? "" : " none but loopback");

        string_format(say, "%slearned %s\n", prefix, (string_address)ago);
}

/* ---- the local network: mDNS and DNS-SD, as records ---- */

/*
        Finding machines on the local network over mDNS and DNS-SD -- the
        protocol printers announce themselves with, so `dns-sd -B` on a Mac
        or avahi-browse on Linux lists a machine that announces. The service
        is the caller's (discovery_mdns_service): its type as it sits on the
        wire, and what the random labels of an instance and a host start
        with. Waterlink's groups are the first to announce one, as
        _waterlink._udp (src/waterlink/group.c).

        The records are parsed the way the most exposed parser on the machine
        must be: every length checked against what is left, a compression
        pointer followed only backwards and at most a few times, a name held
        to 255 bytes and a label to 63, label types other than plain refused,
        and nothing taken from a record that another check still depends on.
        An address is never read out of a record at all -- the A record and
        the SRV target are whatever the sender wrote -- so the caller connects
        to the packet's own source, on the port the SRV names.

        This part is a transform: the sockets are the part after it.
*/
#define DISCOVERY_MDNS_PORT 5353
#define DISCOVERY_MDNS_GROUP 0xe00000fbu // 224.0.0.251
#define DISCOVERY_MDNS_MAX 1500

typedef struct
{
        const p8 address_to type; // the DNS-SD type on the wire, its root with it: "\x0a_waterlink\x04_udp\x05local" and the zero
        positive type_bytes;
        string_address label; // what an instance's and a host's random label starts with: "wl-"
} discovery_mdns_service;

// Writing ---------------------------------------------------------------

/*
        The records are a fixed layout, at most a few hundred bytes with an
        echoed question in front, so they are stored straight into the
        packet: a head, a label of the service's own start and hex, a
        pointer back to a name already written, a record's type, class, TTL
        and length.
*/
static p8 address_to discovery_mdns_head(p8 address_to at, p16 id, p16 flags,
                                         p16 questions, p16 answers)
{
        network_store_16(at, id);
        network_store_16(at + 2, flags);
        network_store_16(at + 4, questions);
        network_store_16(at + 6, answers);
        memory_zero(at + 8, 4);
        return at + 12;
}

static p8 address_to discovery_mdns_label(p8 address_to at, string_address starts,
                                          p8 address_to random, positive bytes)
{
        positive length = string_length(starts);

        at[0] = (p8)(length + 2 * bytes);
        memory_copy(at + 1, starts, length);
        return at + 1 + length + memory_into_hex(at + 1 + length, random, bytes);
}

static p8 address_to discovery_mdns_pointer(p8 address_to at, p8 address_to packet,
                                            p8 address_to name)
{
        network_store_16(at, (p16)(0xc000 | (name - packet)));
        return at + 2;
}

static p8 address_to discovery_mdns_record(p8 address_to at, p16 type, p16 class,
                                           p32 ttl, p16 length)
{
        network_store_16(at, type);
        network_store_16(at + 2, class);
        network_store_32(at + 4, ttl);
        network_store_16(at + 8, length);
        return at + 10;
}

/*
        This machine's announcement of a service, or with ttl 0 its goodbye:
        PTR from the service to the instance, SRV to the host on the port,
        an empty TXT (DNS-SD asks for one), and the host's A record when
        there is an address to give. The instance label is the service's
        start ("wl-") and twenty hex digits, the host label that start and
        twelve, both random and chosen by the caller. A reply to a one-shot
        query carries its ID and its question. Returns the length, or 0 when
        it would not fit.
*/
positive discovery_mdns_announce(const discovery_mdns_service address_to service,
                                 p8 address_to packet, positive room,
                                 p8 address_to instance_bytes,
                                 p8 address_to host_bytes, p16 port,
                                 p32 address, p32 ttl, p16 id,
                                 p8 address_to question, positive question_length)
{
        positive starts = string_length(service->label);
        //      What the records take at most: 160 bytes holds a service of
        //      waterlink's size, and a longer one says its own.
        positive reserve = service->type_bytes + 2 * starts + 100 > 160
                               ? service->type_bytes + 2 * starts + 100
                               : 160;
        p8 address_to at;
        p8 address_to type;
        p8 address_to instance;
        p8 address_to host;

        /* Subtract only after proving the fixed reserve fits: the caller's
           question length must not wrap the capacity check before the copy. */
        if (starts + 20 > 63 || room < 12 + reserve ||
            question_length > room - (12 + reserve) ||
            (!question && question_length) || (question && !question_length))
                return 0;
        at = discovery_mdns_head(packet, id, 0x8400, question ? 1 : 0,
                                3 + (address ? 1 : 0));
        memory_copy(at, question, question_length);
        at += question_length;

        //      PTR: the service, to the instance.
        type = at;
        memory_copy(at, service->type, service->type_bytes);
        at = discovery_mdns_record(at + service->type_bytes, 12, 1,
                                   ttl ? 4500 : 0, (p16)(1 + starts + 20 + 2));
        instance = at;
        at = discovery_mdns_label(at, service->label, instance_bytes, 10);
        at = discovery_mdns_pointer(at, packet, type);

        //      SRV: the instance, to the host, on the port.
        at = discovery_mdns_pointer(at, packet, instance);
        at = discovery_mdns_record(at, 33, 0x8001, ttl,
                                   (p16)(6 + 1 + starts + 12 + 7));
        network_store_32(at, 0);
        network_store_16(at + 4, port);
        host = at + 6;
        at = discovery_mdns_label(host, service->label, host_bytes, 6);
        memory_copy(at, "\x05local", 7);
        at += 7;

        //      TXT: empty, one zero byte.
        at = discovery_mdns_pointer(at, packet, instance);
        at = discovery_mdns_record(at, 16, 0x8001, ttl ? 4500 : 0, 1);
        *at++ = 0;

        if (address)
        {
                at = discovery_mdns_pointer(at, packet, host);
                at = discovery_mdns_record(at, 1, 0x8001, ttl, 4);
                network_store_32(at, address);
                at += 4;
        }
        return (positive)(at - packet);
}

// The question every browser of the service asks.
positive discovery_mdns_query(const discovery_mdns_service address_to service,
                              p8 address_to packet, positive room)
{
        p8 address_to at;

        if (room < 12 + service->type_bytes + 4)
                return 0;
        at = discovery_mdns_head(packet, 0, 0, 1, 0);
        memory_copy(at, service->type, service->type_bytes);
        at += service->type_bytes;
        network_store_16(at, 12);
        network_store_16(at + 2, 1);
        return (positive)(at + 4 - packet);
}

// Reading ------------------------------------------------------------------

/*
        A name at offset at, flattened into labels as lengths and bytes the
        way it sits on the wire uncompressed -- net.c's reader, the one the
        resolver trusts: a pointer only ever goes back past where it stands,
        and nothing after it may read up to it again, so no loop is possible.
        Returns the offset just past the name as it sits in the packet, or 0
        for anything that is not a name or would take more compression
        pointers than the packet's shared work budget has left.
*/
#define DISCOVERY_MDNS_NAME_BYTES 256

static positive discovery_mdns_name(const p8 address_to packet, positive length,
                                   positive at, p8 address_to into,
                                   positive address_to into_length,
                                   positive address_to steps)
{
        positive ended;
        bipolar used = dns_copy_name((p8 address_to)packet, length, at, into,
                                     DISCOVERY_MDNS_NAME_BYTES - 1, address_of ended,
                                     steps);

        if (used < 0)
                return 0;
        address_to into_length = (positive)used;
        return ended;
}

/* The one name that fills a record's data from where data stands to the
   record's end, flattened into into; false for anything else. */
static p8 discovery_mdns_rdata_name(const p8 address_to packet, positive length,
                                 positive stop,
                                 const byte_reader address_to data,
                                 p8 address_to into,
                                 positive address_to into_length,
                                 positive address_to steps)
{
        return discovery_mdns_name(packet, length,
                                  stop - byte_reader_left(data), into,
                                  into_length, steps) == stop;
}

// Internet class; the top bit is QU in a question and cache-flush in a record.
static p8 discovery_mdns_internet(p16 class)
{
        return (class & 0x7fff) == DNS_CLASS_IN;
}

// Whether a flattened name is the service's, letter case aside.
static p8 discovery_mdns_is_service(const discovery_mdns_service address_to service,
                                    const p8 address_to name, positive length)
{
        return length == service->type_bytes &&
               !memory_compare_ascii_case(name, service->type, length);
}

// An instance of the service: one label and then the service's name.
static p8 discovery_mdns_instance_of(const discovery_mdns_service address_to service,
                                     const p8 address_to name, positive length,
                                     p8 address_to label,
                                     positive address_to label_length)
{
        positive first = name[0];

        if (!first || 1 + first >= length ||
            !discovery_mdns_is_service(service, name + 1 + first,
                                       length - 1 - first))
                return false;
        memory_copy(label, name + 1, first);
        address_to label_length = first;
        return true;
}

#define DISCOVERY_MDNS_FOUND_MAX 8

struct discovery_mdns_instance {
        p8 label[63];
        positive label_length;
        p16 port;
        p8 advertised;
        p8 has_port;
};

struct discovery_mdns_found {
        p16 id;
        p8 response;
        p8 asked; // a question for the service
        p8 question[DISCOVERY_MDNS_NAME_BYTES + 4];
        positive question_length;
        positive count;
        struct discovery_mdns_instance instance[DISCOVERY_MDNS_FOUND_MAX];
};

static struct discovery_mdns_instance address_to
discovery_mdns_found_at(struct discovery_mdns_found address_to found,
                   const p8 address_to label, positive label_length)
{
        for (positive at = 0; at < found->count; at++)
                if (found->instance[at].label_length == label_length &&
                    !memory_compare(found->instance[at].label, label,
                                    label_length))
                        return found->instance + at;

        if (found->count == DISCOVERY_MDNS_FOUND_MAX)
                return null;
        memory_zero(found->instance + found->count,
                    sizeof(found->instance[0]));
        memory_copy(found->instance[found->count].label, label, label_length);
        found->instance[found->count].label_length = label_length;
        return found->instance + found->count++;
}

/*
        Read one mDNS message for a service: whether it asks about the
        service, and every instance of the service it describes with an SRV
        port. False, with
        nothing found, when it is not a well formed message; a well formed
        one about other things answers true with nothing found.

        Anyone on the link sends these, so all names share a compression
        pointer budget equal to the packet's wire length: a hundred records
        naming the end of one long chain cost 45 us without it. Ordinary
        labels consume packet bytes and do not spend this second budget.
*/
p8 discovery_mdns_read(const discovery_mdns_service address_to service,
                       const p8 address_to packet, positive length,
                       struct discovery_mdns_found address_to found)
{
        p8 name[DISCOVERY_MDNS_NAME_BYTES];
        positive name_length;
        positive at = 12;
        byte_reader header = byte_reader_open(packet, length);
        p16 flags;
        p32 questions, records;
        positive steps = length;

        memory_zero(found, sizeof(address_to found));
        if (length < 12 || length > DISCOVERY_MDNS_MAX)
                return false;

        found->id = (p16)byte_reader_uint(&header, 2, true);
        flags = (p16)byte_reader_uint(&header, 2, true);
        found->response = (flags & DNS_FLAG_RESPONSE) != 0;
        /* Opcode and rcode are zero in both mDNS message directions.  RCODE
           is not spare query space: accepting a query with an error code
           made malformed packets which compliant responders ignore another
           way to reach this responder and its amplification budget. */
        if (flags & (DNS_OPCODE_MASK | DNS_CODE_MASK))
                goto refused;

        questions = (p16)byte_reader_uint(&header, 2, true);
        records = (p16)byte_reader_uint(&header, 2, true);
        records += (p16)byte_reader_uint(&header, 2, true);
        records += (p16)byte_reader_uint(&header, 2, true);

        //      Each question is at least five bytes and each record eleven:
        //      a count the packet cannot hold is refused before any is read.
        if (questions * 5 + records * 11 > length - 12)
                goto refused;

        for (p32 q = 0; q < questions; q++)
        {
                positive start = at;
                positive next = discovery_mdns_name(packet, length, at, name,
                                                   address_of name_length,
                                                   address_of steps);
                byte_reader tail = dns_message_at(packet, length, next);
                p32 type;
                p16 class;

                //      The type and the class, after a name that was one.
                type = (p16)byte_reader_uint(&tail, 2, true);
                class = (p16)byte_reader_uint(&tail, 2, true);
                if (!next || !byte_reader_ok(&tail))
                        goto refused;
                if (!found->response &&
                    discovery_mdns_is_service(service, name, name_length) &&
                    (type == 12 || type == 255) && discovery_mdns_internet(class))
                {
                        byte_reader echo = dns_message_at(packet, length, start);
                        const p8 address_to whole;

                        found->asked = true;
                        //      The question, kept whole and uncompressed, for
                        //      a reply that has to echo it.
                        if (next - start == name_length &&
                            name_length + 4 <= sizeof(found->question) &&
                            (whole = byte_reader_take(&echo, name_length + 4)))
                        {
                                memory_copy(found->question, whole,
                                            name_length + 4);
                                found->question_length = name_length + 4;
                        }
                }
                at = length - byte_reader_left(&tail);
        }

        for (p32 r = 0; r < records; r++)
        {
                positive next = discovery_mdns_name(packet, length, at, name,
                                                   address_of name_length,
                                                   address_of steps);
                byte_reader tail = dns_message_at(packet, length, next);
                byte_reader data;
                p32 type;
                p16 class;
                p32 ttl;
                p8 label[63];
                positive label_length;
                p8 target[DISCOVERY_MDNS_NAME_BYTES];
                positive target_length = 0;
                struct discovery_mdns_instance address_to instance;

                //      The type, class and time to live, then the data
                //      behind its length.
                type = (p16)byte_reader_uint(&tail, 2, true);
                class = (p16)byte_reader_uint(&tail, 2, true);
                ttl = (p32)byte_reader_uint(&tail, 4, true);
                data = byte_reader_vector16(&tail);
                if (!next || !byte_reader_ok(&tail))
                        goto refused;
                at = length - byte_reader_left(&tail);

                if (!found->response)
                        continue;

                /* DNS-SD discovers an instance through the service's PTR.
                   An orphan SRV is not an advertisement merely because its
                   owner has the service suffix. Keep PTR and SRV independent
                   while walking: records may arrive in either order. */
                if (type == 12 && ttl && discovery_mdns_internet(class) &&
                    discovery_mdns_is_service(service, name, name_length))
                {
                        if (discovery_mdns_rdata_name(packet, length, at,
                                                 address_of data, target,
                                                 address_of target_length,
                                                 address_of steps) &&
                            discovery_mdns_instance_of(service, target,
                                                       target_length, label,
                                                       address_of label_length))
                        {
                                instance = discovery_mdns_found_at(found, label,
                                                              label_length);
                                if (instance)
                                        instance->advertised = true;
                        }
                        continue;
                }

                if (!discovery_mdns_instance_of(service, name, name_length, label,
                                                address_of label_length))
                        continue;

                /* TTL zero is a goodbye, not a fresh endpoint. Treating it
                   as discovery made a peer's departure launch a handshake
                   and let packets caches deliberately discard drive hidden
                   curve work here. */
                if (type == 33 && ttl && discovery_mdns_internet(class) &&
                    byte_reader_left(&data) >= 7)
                {
                        p16 port;

                        //      Priority and weight, then the port.
                        (void)byte_reader_skip(&data, 4);
                        port = (p16)byte_reader_uint(&data, 2, true);

                        /* An SRV RDATA ends in exactly one target name. Root
                           means the service is unavailable, and port zero is
                           not a destination this UDP service can use. The old
                           parser ignored the target and accepted both shapes,
                           letting records other DNS-SD peers discard spend a
                           cryptographic greeting. */
                        if (!port ||
                            !discovery_mdns_rdata_name(packet, length, at,
                                                  address_of data, target,
                                                  address_of target_length,
                                                  address_of steps) ||
                            target_length <= 1)
                                continue;
                        instance = discovery_mdns_found_at(found, label, label_length);
                        if (instance)
                        {
                                instance->port = port;
                                instance->has_port = true;
                        }
                }
        }

        if (at != length)
                goto refused;

        /* Do not expose half of DNS-SD's identity relation. An attacker can
           order the pair either way, so compact only after all records have
           been seen. */
        {
                positive kept = 0;

                for (positive i = 0; i < found->count; i++)
                        if (found->instance[i].advertised &&
                            found->instance[i].has_port)
                                found->instance[kept++] = found->instance[i];
                found->count = kept;
        }
        return true;

        //      A packet refused says nothing: not the question, not the
        //      instances read before the byte that broke it.
refused:
        memory_zero(found, sizeof(address_to found));
        return false;
}

// The end of the records -----------------------------------------------------

/* ---- the local network: its socket ---- */

/*
        The socket the records travel on: UDP 5353, a member of 224.0.0.251
        on every interface that takes the membership. IPv4 only for now;
        ff02::fb is not asked or answered. A packet is believed only if it
        arrived with the TTL it was sent with, 255, which no router forwards:
        that is what keeps this on the local link. An address is taken from
        where a packet came from, never from what it says.

        Linux asks the kernel directly, an interface named by its index
        (ip_mreqn). Darwin has neither the call numbers nor the layouts, so
        there each call is libc's with Darwin's own records, an interface
        named by its IPv4 address (ip_mreq); the Mac's listener includes
        this file after the headers that declare them (service.c).
*/

/*      The kernel's numbers for the socket options and control messages the
        socket asks for, by the names the uapi headers give them;
        wire_constants holds each to the headers. */
#if defined(MACOS)
/*      Darwin's numbers (sys/socket.h, netinet/in.h). IP_PKTINFO is the option
        that turns the destination info on, which Darwin calls IP_RECVPKTINFO. */
#define SO_REUSEPORT 0x0200
#define IP_TTL 4
#define IP_PKTINFO 26
#define IP_RECVTTL 24
#define IP_MULTICAST_IF 9
#define IP_MULTICAST_TTL 10
#define IP_ADD_MEMBERSHIP 12
#else
#define SO_REUSEPORT 15
#define IP_TTL 2
#define IP_PKTINFO 8
#define IP_RECVTTL 12
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_ADD_MEMBERSHIP 35
#endif

/*      The kernel's record for a message with its parts and what came
        beside it (struct msghdr and struct iovec, as Linux lays them out),
        and the walk of the control messages beside a datagram. The
        listener's own socket reads its batches through these too
        (src/waterlink/service.c). */
typedef struct
{
        address_any base;
        positive length;
} socket_iovec;

typedef struct
{
        address_any name;
        b32 name_length;
        b32 padding;
        socket_iovec address_to parts;
        positive part_count;
        address_any control;
        positive control_length;
        b32 flags;
        b32 padding2;
} socket_message;

/*
        The data of the first control message with this level and type, among
        the length bytes the kernel filled -- not the room the buffer has,
        which holds whatever an earlier call left. Each cmsghdr is a length
        that counts itself, a level and a type, and is padded to eight bytes;
        a message that claims more than was filled ends the walk. True with
        data a reader on the message's bytes.
*/
static p8 socket_control_find(p8 address_to control, positive length,
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
static b32 socket_control_int(p8 address_to control, positive length,
                              positive room, b32 level, b32 type)
{
        byte_reader data;

        return socket_control_find(control, length, room, level, type,
                                   address_of data) &&
                       byte_reader_left(&data) >= 4
                       ? (b32)(p32)byte_reader_uint(&data, 4, false)
                       : -1;
}

#define DISCOVERY_LAN_INTERFACES 16

//      What discovery_lan_reach found this machine can use.
#define DISCOVERY_LAN_ROUTE 1
#define DISCOVERY_LAN_JOINS 2

typedef struct
{
        bipolar socket;
        b32 interface[DISCOVERY_LAN_INTERFACES];
        p32 address[DISCOVERY_LAN_INTERFACES]; // host order
        p32 mask[DISCOVERY_LAN_INTERFACES];    // host order; 0 when not known
        positive interfaces;
        positive ask; // the switches' DISCOVERY_ASK_ANNOUNCE and _LISTEN, which its caller reads
} discovery_lan;

/*      Whether the switches let a packet leave: an mDNS response (the QR
        bit of its header) is an announcement or an answer, which LAN
        announce allows, and a question is asking, which LAN listen allows.
        A packet too short to say is neither. The socket's two senders ask
        it before anything is counted or sent, so with a switch off no
        packet of its kind leaves, whatever its caller does. */
static p8 discovery_lan_allowed(const discovery_lan address_to lan,
                                const p8 address_to packet, positive length)
{
        if (length < 12)
                return false;
        return (packet[2] & 0x80) ? (lan->ask & DISCOVERY_ASK_ANNOUNCE) != 0
                                  : (lan->ask & DISCOVERY_ASK_LISTEN) != 0;
}

#if !defined(MACOS)
//      struct ip_mreqn: the group, the interface's address and its index.
typedef struct
{
        p32 multiaddr;
        p32 address;
        b32 index;
} discovery_mreqn;
#endif

/*      A socket option set as socket_option_set sets it: zero, or the negative
        errno. On Darwin it is setsockopt from libc, with Darwin's errno, so
        the "already joined" answer of a membership is Darwin's EADDRINUSE. */
static bipolar discovery_option_set(b32 handle, b32 level, b32 name,
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

/*      An IPv4 datagram socket that does not wait and is not inherited
        across an exec. Linux says both in the type it asks for; Darwin's
        socket() takes no flags, so there it is libc's and two fcntl calls. */
static bipolar discovery_lan_socket(void)
{
#if defined(MACOS)
        bipolar handle = socket(AF_INET, SOCK_DGRAM, 0);

        if (handle >= 0)
        {
                (void)fcntl((int)handle, F_SETFD, FD_CLOEXEC);
                (void)fcntl((int)handle, F_SETFL, O_NONBLOCK);
        }
        return handle;
#else
        return socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#endif
}

/*
        Every interface with an IPv4 address, handed to visit with its index,
        that address in host order and its netmask. Linux reads them as
        discovery reads every address, by net.c's walk of the kernel's table
        (discovery_here_read_some); Darwin has no netlink, and walks them
        with getifaddrs, which is the BSD routine for the same list. The
        socket asks each for the membership and keeps its address for the A
        record (discovery_lan_join); waterlink's doctor and its status ask
        the same walk what this machine can reach, so they say what the
        listener finds.
*/
typedef fn (address_to discovery_interface_visitor)(address_any context, b32 index,
                                                    p32 address, p32 mask);

#if !defined(MACOS)
//      Each IPv4 address a walk gathered, to visit: no other family's.
static fn discovery_interfaces_visit(const discovery_here address_to here,
                                     discovery_interface_visitor visit,
                                     address_any context)
{
        for (positive at = 0; at < here->owns; at++)
        {
                p8 prefix = here->own[at].prefix;

                if (!discovery_mapped(here->own[at].address))
                        continue;
                visit(context, here->own[at].index,
                      network_load_32((p8 address_to)here->own[at].address + 12),
                      prefix && prefix <= 32 ? 0xffffffffu << (32 - prefix) : 0);
        }
}
#endif

static fn discovery_interfaces_each(discovery_interface_visitor visit,
                                    address_any context)
{
#if defined(MACOS)
        struct ifaddrs *list = null;

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
                visit(context, index, network_load_32(raw + 4), mask);
        }
        freeifaddrs(list);
#else
        discovery_here here;

        discovery_here_read_some(address_of here, false, AF_INET);
        discovery_interfaces_visit(address_of here, visit, context);
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
static fn discovery_lan_join(address_any context, b32 index, p32 address, p32 mask)
{
        discovery_lan address_to lan = context;
#if defined(MACOS)
        link_ip_mreq join;
#else
        discovery_mreqn join = {network_order_32(DISCOVERY_MDNS_GROUP), 0, 0};
#endif
        bipolar joined;

        for (positive at = 0; at < lan->interfaces; at++)
                if (lan->interface[at] == index)
                        return;
        if (lan->interfaces == DISCOVERY_LAN_INTERFACES)
                return;
#if defined(MACOS)
        join.multiaddr = network_order_32(DISCOVERY_MDNS_GROUP);
        join.interface = network_order_32(address);
#else
        join.index = index;
#endif
        joined = discovery_option_set((b32)lan->socket, IPPROTO_IP,
                                      IP_ADD_MEMBERSHIP, address_of join,
                                      sizeof join);
        if (joined < 0 && joined != -EADDRINUSE)
                return;
        lan->interface[lan->interfaces] = index;
        lan->address[lan->interfaces] = address;
        lan->mask[lan->interfaces] = mask;
        lan->interfaces++;
}

//      The memberships asked for again, of the interfaces as they are now.
static fn discovery_lan_interfaces(discovery_lan address_to lan)
{
        lan->interfaces = 0;
        discovery_interfaces_each(discovery_lan_join, lan);
}

static fn discovery_lan_close(discovery_lan address_to lan)
{
        if (lan->socket > 0)
                socket_close((b32)lan->socket);
        lan->socket = -1;
        lan->interfaces = 0;
}

//      The socket, bound to the port and joined on every interface that
//      takes the membership; lan->socket is negative when there is none.
static fn discovery_lan_open(discovery_lan address_to lan)
{
        b32 one = 1;
        b32 ttl = 255;
#if !defined(MACOS)
        socket_address_internet self = {
            .family = AF_INET, .port = network_order_16(DISCOVERY_MDNS_PORT)};
#endif

        lan->socket = discovery_lan_socket();
        if (lan->socket < 0)
                return;
        (void)discovery_option_set((b32)lan->socket, SOL_SOCKET, SO_REUSEADDR,
                                   address_of one, sizeof one);
        (void)discovery_option_set((b32)lan->socket, SOL_SOCKET, SO_REUSEPORT,
                                   address_of one, sizeof one);
        (void)discovery_option_set((b32)lan->socket, IPPROTO_IP, IP_MULTICAST_TTL,
                                   address_of ttl, sizeof ttl);
        (void)discovery_option_set((b32)lan->socket, IPPROTO_IP, IP_RECVTTL,
                                   address_of one, sizeof one);
        (void)discovery_option_set((b32)lan->socket, IPPROTO_IP, IP_PKTINFO,
                                   address_of one, sizeof one);

#if defined(MACOS)
        {
                struct sockaddr_in local;

                memory_zero(address_of local, sizeof local);
                local.sin_len = (unsigned char)sizeof local;
                local.sin_family = AF_INET;
                local.sin_port = network_order_16(DISCOVERY_MDNS_PORT);
                if (bind((b32)lan->socket, (struct sockaddr *)address_of local,
                         sizeof local) < 0)
                {
                        discovery_lan_close(lan);
                        return;
                }
        }
#else
        if (socket_bind((b32)lan->socket, address_of self, sizeof self) < 0)
        {
                discovery_lan_close(lan);
                return;
        }
#endif
        discovery_lan_interfaces(lan);
}

/*      One packet to the group, out of one interface the socket has joined
        on: its length when it went, or the negative errno of what stopped
        it (DISCOVERY_LAN_OFF when a switch is off for it). Counted in
        discovery_sent. */
#define DISCOVERY_LAN_OFF (-1) // EPERM, on both kernels

static bipolar discovery_lan_send(discovery_lan address_to lan, positive interface_at,
                                  p8 address_to packet, positive length)
{
        if (!discovery_lan_allowed(lan, packet, length))
                return DISCOVERY_LAN_OFF;
#if defined(MACOS)
        /* Darwin names the interface by its address, in network order, and
           its sockaddr_in carries a length first. Both are built here, for
           libc, since the lib's socket address is the Linux layout. */
        struct in_addr out;
        struct sockaddr_in to;

        memory_zero(address_of to, sizeof to);
        to.sin_len = (unsigned char)sizeof to;
        to.sin_family = AF_INET;
        to.sin_port = network_order_16(DISCOVERY_MDNS_PORT);
        to.sin_addr.s_addr = network_order_32(DISCOVERY_MDNS_GROUP);
        out.s_addr = network_order_32(lan->address[interface_at]);

        /* IP_MULTICAST_IF is part of the destination, not a best-effort hint.
           If it fails, send would retain the preceding interface selection
           and put this interface's announcement onto another link. */
        bipolar set = discovery_option_set((b32)lan->socket, IPPROTO_IP,
                                           IP_MULTICAST_IF, address_of out,
                                           sizeof out);
        ssize_t sent;

        if (set < 0)
                return set;
        discovery_sent++;
        sent = sendto((b32)lan->socket, packet, length, 0,
                      (struct sockaddr *)address_of to, sizeof to);
        return sent < 0 ? -errno : (bipolar)sent;
#else
        discovery_mreqn out = {0, 0, lan->interface[interface_at]};
        socket_address_internet to = {
            .family = AF_INET, .port = network_order_16(DISCOVERY_MDNS_PORT),
            .host = network_order_32(DISCOVERY_MDNS_GROUP)};

        /* IP_MULTICAST_IF is part of the destination, not a best-effort hint.
           If it fails, send would retain the preceding interface selection
           and put this interface's announcement onto another link. */
        bipolar set = socket_option_set((b32)lan->socket, IPPROTO_IP,
                                        IP_MULTICAST_IF, address_of out, sizeof out);

        if (set < 0)
                return set;
        discovery_sent++;
        return socket_send((b32)lan->socket, packet, length, MSG_NOSIGNAL,
                           address_of to, sizeof to);
#endif
}

//      One packet back to the one place that asked, as a one-shot asker is
//      answered: its length when it went, DISCOVERY_LAN_OFF when LAN
//      announce is off. Counted in discovery_sent.
static bipolar discovery_lan_reply(discovery_lan address_to lan, p32 address, p16 port,
                                   p8 address_to packet, positive length)
{
        socket_address_internet to = {.family = AF_INET,
                                      .port = network_order_16(port),
                                      .host = network_order_32(address)};

        if (!discovery_lan_allowed(lan, packet, length))
                return DISCOVERY_LAN_OFF;
#if defined(MACOS)
        to.length = sizeof to;
#endif
        discovery_sent++;
        return socket_send((b32)lan->socket, packet, length, MSG_NOSIGNAL,
                           address_of to, sizeof to);
}

/*
        What this machine can use of the local network, asked of every
        interface but loopback as the socket asks it (discovery_lan_join,
        discovery_lan_send): ROUTE when a datagram to the group has a way out
        of one, JOINS when one takes the group's membership. Each is asked
        with the interface named, on a socket closed straight after, and
        nothing is sent. Asked of no interface the kernel answers from its
        routes, and a network with no default route -- two machines on a
        cable, a segment that goes nowhere else -- has none to 224.0.0.251:
        a pairing code was refused there with "no route to a network" and
        nothing sent, on a network the listener was announcing on.
*/
static fn discovery_lan_reach_one(address_any context, b32 index, p32 address,
                                  p32 mask)
{
        positive address_to found = context;
#if defined(MACOS)
        struct in_addr out = {network_order_32(address)};
        link_ip_mreq join = {network_order_32(DISCOVERY_MDNS_GROUP),
                             network_order_32(address)};
#else
        discovery_mreqn out = {0, 0, index};
        discovery_mreqn join = {network_order_32(DISCOVERY_MDNS_GROUP), 0, index};
#endif
        socket_address_internet to = {
            .family = AF_INET, .port = network_order_16(DISCOVERY_MDNS_PORT),
            .host = network_order_32(DISCOVERY_MDNS_GROUP)};
        bipolar handle = (address >> 24) == 127 ? -1 : discovery_lan_socket();
        bipolar joined;

        (void)index;
        (void)mask;
        if (handle < 0)
                return;
#if defined(MACOS)
        to.length = sizeof to;
#endif
        if (discovery_option_set((b32)handle, IPPROTO_IP, IP_MULTICAST_IF,
                                 address_of out, sizeof out) >= 0 &&
            socket_connect((b32)handle, address_of to, sizeof to) >= 0)
                address_to found |= DISCOVERY_LAN_ROUTE;
        joined = discovery_option_set((b32)handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                      address_of join, sizeof join);
        if (joined >= 0 || joined == -EADDRINUSE)
                address_to found |= DISCOVERY_LAN_JOINS;
        socket_close((b32)handle);
}

static positive discovery_lan_reach(void)
{
        positive found = 0;

        discovery_interfaces_each(discovery_lan_reach_one, address_of found);
        return found;
}

#if !defined(MACOS)
/* The local IPv4 address the kernel says received a packet. IP_PKTINFO's
   data is ifindex, spec_dst and the header's destination, each four bytes;
   spec_dst is the local address selected for the incoming packet. The
   length the kernel filled bounds the read, not the room the buffer has. */
static p32 discovery_lan_local(p8 address_to control, positive length, positive room)
{
        byte_reader data;

        if (!socket_control_find(control, length, room, IPPROTO_IP, IP_PKTINFO,
                                 address_of data) ||
            byte_reader_left(&data) < 12)
                return 0;
        (void)byte_reader_skip(&data, 4);
        return (p32)byte_reader_uint(&data, 4, true);
}
#endif

/*
        One datagram that was waiting on the socket, into packet, which has
        room for DISCOVERY_MDNS_MAX and a byte: its length, with where it came
        from and the local address it arrived at (0 when not known), both in
        host order; 0 for one to pass over (longer than a message may be, or
        not arrived with TTL 255); negative when nothing waits.

        Darwin's is recvmsg from libc: the same call with the same fields as
        Linux's -- the sender's address, one buffer, and a control buffer for
        the TTL and the receiving interface -- in Darwin's layouts, which
        differ in three places:

          struct msghdr has its control length in a 32-bit field, its flags
          after it, and a 32-bit iovec count, so it is the libc struct and
          not the Linux record socket_message;
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
*/
static bipolar discovery_lan_receive(discovery_lan address_to lan, p8 address_to packet,
                                     p32 address_to from_address,
                                     p16 address_to from_port, p32 address_to arrived)
{
#if defined(MACOS)
        struct sockaddr_in from;
        struct iovec part = {packet, DISCOVERY_MDNS_MAX + 1};
        p64 control[8];
        struct msghdr message;
        struct cmsghdr address_to header;
        ssize_t got;
        b32 ttl = -1;

        address_to arrived = 0;
        memory_zero(address_of from, sizeof from);
        memory_zero(address_of message, sizeof message);
        message.msg_name = address_of from;
        message.msg_namelen = sizeof from;
        message.msg_iov = address_of part;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof control;
        got = recvmsg((int)lan->socket, address_of message, MSG_DONTWAIT);
        if (got < 0)
                return -1;
        if ((positive)got > DISCOVERY_MDNS_MAX)
                return 0;

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

                        memory_copy_apart(address_of index, CMSG_DATA(header),
                                          sizeof index);
                        for (positive at = 0; at < lan->interfaces; at++)
                                if (lan->interface[at] == index)
                                        address_to arrived = lan->address[at];
                }
        }
        if (ttl != 255)
                return 0;
        address_to from_address = network_order_32(from.sin_addr.s_addr);
        address_to from_port = network_order_16(from.sin_port);
        return (bipolar)got;
#else
        socket_address_internet from;
        socket_iovec part = {packet, DISCOVERY_MDNS_MAX + 1};
        p64 control[8];
        socket_message message;
        bipolar got;

        address_to arrived = 0;
        memory_zero(address_of message, sizeof message);
        message.name = address_of from;
        message.name_length = sizeof from;
        message.parts = address_of part;
        message.part_count = 1;
        message.control = control;
        message.control_length = sizeof control;
        got = system_call_3(syscall(recvmsg), (positive)lan->socket,
                            (positive)address_of message, MSG_DONTWAIT);
        if (got < 0)
                return -1;
        if ((positive)got > DISCOVERY_MDNS_MAX)
                return 0;
        if (socket_control_int((p8 address_to)control, message.control_length,
                               sizeof control, IPPROTO_IP, IP_TTL) != 255)
                return 0;
        address_to arrived = discovery_lan_local((p8 address_to)control,
                                               message.control_length,
                                               sizeof control);
        address_to from_address = network_order_32(from.host);
        address_to from_port = network_order_16(from.port);
        return got;
#endif
}

// The end of the socket ---------------------------------------------------------

/* ---- the local network: its reader, apart ---- */

/*
        Every host on the link may send this machine mDNS, and reading it is a
        walk of names, compression pointers and records of the sender's own
        lengths (discovery_mdns_read). Whoever listens is root and holds what
        root holds -- waterlink's listener keeps the link's key and runs
        commands for linked machines -- so the walk runs in a reader of its
        own: a child that forgets what its parent held (forget, the caller's),
        is nobody, cannot be dumped, dies with its parent, holds only its end
        of a socket of whole messages and stdio, and kills itself at any call
        but reading a packet from it and sending the reading back
        (discovery_mdns_filter_build). What it answers is the fixed-size
        discovery_mdns_found, believed only with every count and length
        inside its array. A reader that cannot be confined (no seccomp,
        qemu-user) reads nothing: the caller then hears no machine nearby,
        which is the local network without discovery, not discovery
        unconfined. One that dies or answers what cannot be believed is ended
        and started again for the next packet.

        Linux confines the child with seccomp (net.c's filter, net_apart_nobody
        and net_apart_tie). Darwin has no seccomp, so its child is confined by
        Seatbelt (discovery_mdns_confine, below). The two differ in that one
        function and in the socket kind the pair is made with; everything
        else in the reader, the parent's side included, is the same code on
        both systems. A child that cannot be confined on either reads
        nothing, so a Mac that cannot confine it hears no machine nearby,
        as a Linux box without seccomp does.
*/
#define DISCOVERY_MDNS_SOCK_SEQPACKET 5
#define DISCOVERY_MDNS_ANSWER_SECONDS 1
/* Darwin's unix socket pairs have no SOCK_SEQPACKET; a datagram is a whole
   message there too, and it is the kind the pair is made with on that system. */
#if defined(MACOS)
#define DISCOVERY_MDNS_SOCK_KIND 2 /* SOCK_DGRAM */
#define DISCOVERY_MDNS_DESCRIPTORS 1024 /* the child closes these bar its channel */
#else
#define DISCOVERY_MDNS_SOCK_KIND (DISCOVERY_MDNS_SOCK_SEQPACKET | SOCK_CLOEXEC)
#endif

struct discovery_mdns_said
{
        b64 read;
        struct discovery_mdns_found found;
};

static struct
{
        b32 channel;
        bipolar pid;
        p8 refused;
} discovery_mdns_reader = {-1, -1, false};

#if !defined(MACOS)
static COLD fn discovery_mdns_filter_build(net_filter address_to filter)
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
static COLD bool discovery_mdns_confine(b32 channel, bipolar parent)
{
        p32 kept[1] = {(p32)channel};
        net_filter filter;

        discovery_mdns_filter_build(address_of filter);
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

#define DISCOVERY_MDNS_PROFILE                                               \
        "(version 1)(deny default)(allow sysctl-read)(allow file-read-metadata)"

static COLD bool discovery_mdns_confine(b32 channel, bipolar parent)
{
        char *error = null;

        (void)parent;
        for (positive fd = 3; fd < DISCOVERY_MDNS_DESCRIPTORS; fd++)
                if (fd != (positive)channel)
                        (void)system_call_1(syscall(close), fd);
        return sandbox_init(DISCOVERY_MDNS_PROFILE, 0, &error) == 0;
}
#endif

static DEAD_END COLD fn discovery_mdns_child(
    const discovery_mdns_service address_to service, fn (address_to forget)(void),
    b32 channel, bipolar parent)
{
        //      Before a byte is read: this process is a copy of its parent,
        //      and what it is kept apart from, it was holding.
        forget();
        if (!discovery_mdns_confine(channel, parent))
                system_call_1(syscall(exit_group), NET_APART_EXIT_UNCONFINED);
        for (;;)
        {
                p8 packet[DISCOVERY_MDNS_MAX + 1];
                struct discovery_mdns_said said;
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
                said.read = discovery_mdns_read(service, packet, (positive)got,
                                                address_of said.found);
                if (socket_send(channel, address_of said, sizeof said, MSG_NOSIGNAL, null, 0) !=
                    (bipolar)sizeof said)
                        break;
        }
        system_call_1(syscall(exit_group), 0);
        unreachable_here();
}

static COLD fn discovery_mdns_reader_end(void)
{
        positive raw = 0;

        if (discovery_mdns_reader.channel >= 0)
                system_close(discovery_mdns_reader.channel);
        if (discovery_mdns_reader.pid > 0)
        {
                system_call_2(syscall(kill), (positive)discovery_mdns_reader.pid, SIGKILL);
                (void)system_wait4_retry(discovery_mdns_reader.pid, address_of raw, 0, null);
        }
        discovery_mdns_reader.channel = -1;
        discovery_mdns_reader.pid = -1;
}

static COLD p8 discovery_mdns_reader_start(
    const discovery_mdns_service address_to service, fn (address_to forget)(void))
{
        b32 ends[2];
        bipolar parent = system_call(syscall(getpid));
        bipolar child;

        if (system_call_4(syscall(socketpair), AF_UNIX, DISCOVERY_MDNS_SOCK_KIND, 0,
                          (positive)ends) < 0)
                return false;
        child = system_fork();
        if (!child)
        {
                system_close(ends[0]);
                discovery_mdns_child(service, forget, ends[1], parent);
        }
        system_close(ends[1]);
        if (child < 0)
        {
                system_close(ends[0]);
                return false;
        }
        discovery_mdns_reader.channel = ends[0];
        discovery_mdns_reader.pid = child;
        return true;
}

static PURE COLD p8 discovery_mdns_believed(const struct discovery_mdns_found address_to found)
{
        if (found->count > DISCOVERY_MDNS_FOUND_MAX ||
            found->question_length > sizeof(found->question))
                return false;
        for (positive at = 0; at < found->count; at++)
                if (found->instance[at].label_length > sizeof(found->instance[at].label))
                        return false;
        return true;
}

/* discovery_mdns_read, apart: the reading the reader made, or false. forget
   is what the reader, a copy of this process, puts out of its memory before
   it reads a byte. */
static COLD p8 discovery_mdns_read_apart(const discovery_mdns_service address_to service,
                                         fn (address_to forget)(void),
                                         p8 address_to packet, positive length,
                                         struct discovery_mdns_found address_to found)
{
        struct discovery_mdns_said said;
        network_deadline deadline;

        memory_zero(found, sizeof(*found));
        //      Shorter than a header is no message, and is not sent: an
        //      empty one reads at the reader as its channel's end, so each
        //      cost the listener a reader, the fork and the confinement of
        //      the next, for a datagram of no bytes from anyone on the link.
        if (discovery_mdns_reader.refused || length < DNS_HEADER ||
            length > DISCOVERY_MDNS_MAX)
                return false;
        if (discovery_mdns_reader.channel < 0 &&
            !discovery_mdns_reader_start(service, forget))
                return false;
        if (socket_send(discovery_mdns_reader.channel, packet, length,
                        MSG_NOSIGNAL | MSG_DONTWAIT, null, 0) != (bipolar)length ||
            !network_deadline_begin(address_of deadline, DISCOVERY_MDNS_ANSWER_SECONDS, 0) ||
            network_wait_readable_until(discovery_mdns_reader.channel, address_of deadline) <= 0 ||
            socket_receive(discovery_mdns_reader.channel, address_of said, sizeof said,
                           MSG_DONTWAIT, null, null) != (bipolar)sizeof said ||
            !discovery_mdns_believed(address_of said.found))
        {
                positive raw = 0;

                //      A reader that could not be confined says so by its exit,
                //      once: there is no reading here from then on. Waited for
                //      here, however it ended, it is not this process's to
                //      signal: its number may be another process's by the
                //      time discovery_mdns_reader_end sends the kill.
                if (discovery_mdns_reader.pid > 0 &&
                    system_wait4_retry(discovery_mdns_reader.pid, address_of raw,
                                       1 /* WNOHANG */, null) == discovery_mdns_reader.pid)
                {
                        discovery_mdns_reader.pid = -1;
                        discovery_mdns_reader.refused =
                            (raw & 0x7f) == 0 &&
                            ((raw >> 8) & 0xff) == NET_APART_EXIT_UNCONFINED;
                }
                discovery_mdns_reader_end();
                return false;
        }
        address_to found = said.found;
        return said.read != 0;
}

#endif // NET_DISCOVERY_INCLUDED
