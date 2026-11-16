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
          mapping, by a STUN binding (RFC 8489) from one socket to two
          servers: one mapping for both is a NAT that maps by endpoint
          (RFC 4787's endpoint-independent mapping, which a hole can be
          punched through), two mappings is one that maps per server;

          where UDP does not get out, the public address over HTTPS, the
          trace Cloudflare answers, fetched and read in a confined child
          (net_apart) as the zone question is;

          whether an HTTP proxy is named (HTTPS_PROXY), which a later piece
          tunnels through by CONNECT.

        Every lookup that sends a packet off the machine has a switch, a
        word in /root that is on when there is none, and discovery's own
        switch is all of them at once (discovery_settings). A lookup that is
        not allowed sends nothing: discovery_sent counts every datagram and
        connection this file opens, and the check holds it where it was with
        discovery off. Every wait is about a second. What a gather finds is
        kept with its time in DISCOVERY_CACHE_PATH, which is root's and in
        /run, so a reader (Settings, the status) never waits on the network,
        and discovery_report says it a line a key, to any writer.

        IPv6's public address is the interface's own global address for now:
        the resolver asks A records alone and the confined child has no
        IPv6 socket, so STUN and the trace over IPv6 come with them.

        Included after net.c: by src/sh/net.c in the shell, and by
        src/waterlink/nearby.c where the link is built without one (a Mac),
        which is why Darwin's side of this file is stubs for STUN, the trace
        and the interface walk and nothing of the shell's is named here.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef NET_DISCOVERY_INCLUDED
#define NET_DISCOVERY_INCLUDED

#include "../sh/host_words.h"

//      The lookups a gather may make: the person's switches.
#define DISCOVERY_ASK_STUN 1u
#define DISCOVERY_ASK_HTTPS 2u

#define DISCOVERY_CACHE_PATH "/run/moonwater/discovery"
#define DISCOVERY_STUN_PORT 3478
#define DISCOVERY_TRACE_URL "https://cloudflare.com/cdn-cgi/trace"

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

static p8 discovery_starts(string_address text, string_address prefix)
{
        positive length = string_length(prefix);

        return text && string_length(text) >= length &&
               !memory_compare(text, prefix, length);
}

/*      An interface's kind, from its name, its hardware type (ARPHRD_*), the
        kernel's word for its driver (IFLA_INFO_KIND, or none) and whether
        it has a wireless side. A VPN is told by its driver or its name
        before its hardware: ZeroTier's is an ethernet tap. */
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
        if (wireless || discovery_starts(name, "wl"))
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

#define DISCOVERY_SCOPE_GLOBAL 0
#define DISCOVERY_SCOPE_PRIVATE 1
#define DISCOVERY_SCOPE_SHARED 2
#define DISCOVERY_SCOPE_LINK 3
#define DISCOVERY_SCOPE_UNIQUE 4
#define DISCOVERY_SCOPE_LOOPBACK 5
#define DISCOVERY_SCOPE_OTHER 6
static const string_address discovery_scope_names[] = {
    "global", "private", "shared", "link-local", "unique local", "loopback",
    "other"};

//      An IPv4 address is held as IPv6 holds it, ::ffff:a.b.c.d.
static p8 discovery_mapped(const p8 address_to address)
{
        static const p8 prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

        return !memory_compare(address, prefix, 12);
}

static positive discovery_address_scope(const p8 address_to address)
{
        static const p8 loopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 1};

        if (discovery_mapped(address))
        {
                p8 a = address[12], b = address[13];

                if (a == 127)
                        return DISCOVERY_SCOPE_LOOPBACK;
                if (a == 10 || (a == 172 && (b & 0xf0) == 16) ||
                    (a == 192 && b == 168))
                        return DISCOVERY_SCOPE_PRIVATE;
                if (a == 100 && (b & 0xc0) == 64)
                        return DISCOVERY_SCOPE_SHARED;
                if (a == 169 && b == 254)
                        return DISCOVERY_SCOPE_LINK;
                if (!a || a >= 224)
                        return DISCOVERY_SCOPE_OTHER;
                return DISCOVERY_SCOPE_GLOBAL;
        }
        if (!memory_compare(address, loopback6, 16))
                return DISCOVERY_SCOPE_LOOPBACK;
        if (address[0] == 0xfe && (address[1] & 0xc0) == 0x80)
                return DISCOVERY_SCOPE_LINK;
        if ((address[0] & 0xfe) == 0xfc)
                return DISCOVERY_SCOPE_UNIQUE;
        if ((address[0] & 0xe0) == 0x20)
                return DISCOVERY_SCOPE_GLOBAL;
        return DISCOVERY_SCOPE_OTHER;
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
static const string_address discovery_nat_names[] = {
    "not known: no STUN server answered",
    "not known: one STUN server answered, and two are needed to tell",
    "none: the public address is this machine's own",
    "endpoint-independent: one mapping for every server, so a hole can be punched",
    "endpoint-dependent: a mapping for each server, so a hole cannot be punched "
    "without the router's help or a relay"};

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
#define DISCOVERY_ADDRESSES 32

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
        p8 path[64];
        p8 driver[16] = {0};
        positive size = 0;
        p8 address_to info;
        discovery_interface address_to into;

        if (header->type != RTM_NEWLINK ||
            here->interfaces >= DISCOVERY_INTERFACES)
                return true;
        name = netlink_link_name(header, address_of link);
        if (!name || !link || string_length(name) >= 16)
                return true;
        info = netlink_find(header, sizeof(netlink_link), IFLA_LINKINFO,
                            address_of size);
        if (info)
        {
                positive kind_size = 0;
                p8 address_to kind = netlink_find_span(info, size, IFLA_INFO_KIND,
                                                       address_of kind_size);

                if (kind && kind_size && kind_size < sizeof driver)
                {
                        memory_copy(driver, kind, kind_size);
                        driver[kind_size] = end;
                }
        }
        //      A wireless interface has a wireless directory in sysfs, whatever
        //      its driver is called.
        string_copy(string_copy(string_copy((string_address)path,
                                            "/sys/class/net/"),
                                name),
                    "/wireless");
        into = here->interface + here->interfaces++;
        into->index = (b32)link->index;
        string_copy_bounded((string_address)into->name, name, sizeof into->name);
        into->kind = (p8)discovery_interface_kind(
            name, link->kind,
            system_call_4(syscall(faccessat), (positive)AT_FDCWD, (positive)path,
                          0, 0) == 0,
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

//      Every interface and every address the kernel has, loopback's too;
//      nothing leaves the machine for it.
static fn discovery_here_read(discovery_here address_to here)
{
        memory_zero(here, sizeof(address_to here));
#if !defined(MACOS)
        bipolar handle = netlink_open_groups(0);

        if (handle < 0)
                return;
        (void)netlink_dump((b32)handle, RTM_GETLINK, sizeof(netlink_link), 0,
                           discovery_link_seen, here);
        (void)netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address), 0,
                           discovery_address_seen, here);
        system_close((b32)handle);
#endif
}

/* ---- the settings: the person's switches ---- */

/*      Each lookup's switch is a word in /root (DISCOVERY_KEPT), on when
        there is none, and discovery's own is every lookup at once: off
        there is every lookup off, whatever its own word says. `moonwater
        network discovery` writes them (src/sh/host/network.c). A word
        `moonwater link discovery` kept, before the switches were the
        network's, is moved to its new name the first time it is read. */
#define DISCOVERY_KEPT "/root"
#define DISCOVERY_PATH_ROOM 96

static const struct
{
        string_address word;
        string_address leaf;
        string_address was;
        positive ask;
        string_address says;
} discovery_switches[] = {
    {"discovery", "network.discovery", "link.discovery", 0, "every lookup below"},
    {"stun", "network.discovery.stun", "link.discovery.stun", DISCOVERY_ASK_STUN,
     "Public address discovery (STUN): a binding to stun.cloudflare.com and "
     "stun.l.google.com"},
    {"https", "network.discovery.https", "link.discovery.https", DISCOVERY_ASK_HTTPS,
     "Public address discovery (HTTPS): cloudflare.com's trace, where UDP "
     "does not get out"},
};

typedef struct
{
        p8 switched[array_count(discovery_switches)]; // each switch's own word
        positive ask; // DISCOVERY_ASK_*: what they allow together, nothing while discovery is off
} discovery_settings;

//      The file a setting is kept in: the directory, a slash and its leaf.
static fn discovery_kept_path(string_address directory, string_address leaf,
                              p8 address_to path)
{
        string_copy_bounded((string_address)path, directory, DISCOVERY_PATH_ROOM);
        string_append_bounded((string_address)path, "/", DISCOVERY_PATH_ROOM);
        string_append_bounded((string_address)path, leaf, DISCOVERY_PATH_ROOM);
}

static fn discovery_settings_read_at(string_address directory,
                                     discovery_settings address_to settings)
{
        memory_zero(settings, sizeof(address_to settings));
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
                p8 path[DISCOVERY_PATH_ROOM];
                p8 word[8];

                discovery_kept_path(directory, discovery_switches[at].leaf, path);
#if !defined(MACOS)
                {
                        p8 was[DISCOVERY_PATH_ROOM];

                        //      Never over a word the network's verb has kept since.
                        discovery_kept_path(directory, discovery_switches[at].was, was);
                        (void)system_rename_at(AT_FDCWD, (string_address)was, AT_FDCWD,
                                               (string_address)path,
                                               1 /* RENAME_NOREPLACE */);
                }
#endif
                settings->switched[at] =
                    host_read_word((string_address)path, word, sizeof word) < 0 ||
                    !string_equals((string_address)word, "off");
        }
        for (positive at = 1; settings->switched[0] && at < array_count(discovery_switches); at++)
                if (settings->switched[at])
                        settings->ask |= discovery_switches[at].ask;
}

static fn discovery_settings_read(discovery_settings address_to settings)
{
        discovery_settings_read_at(DISCOVERY_KEPT, settings);
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
} discovery_found;

static const struct
{
        string_address name;
        p16 port;
} discovery_stun_servers[2] = {{"stun.cloudflare.com", DISCOVERY_STUN_PORT},
                                {"stun.l.google.com", 19302}};

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
                    .port = network_order_16(discovery_stun_servers[at].port),
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
                limit.tv_sec = 0;
                limit.tv_nsec = (b64)(until - now);
                if (descriptor_wait_readable(handle, address_of limit, null) <= 0)
                        continue;
                got = socket_receive((b32)handle, answer, sizeof answer, 0, null, null);
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

/*      One socket, a request to each server and one more after half a
        second to any that has not answered, and a second in all. The same
        socket for both is what lets the two mappings be compared. */
static fn discovery_stun(discovery_found address_to found,
                          const discovery_here address_to here)
{
        p32 server[2] = {0, 0};
        p8 transaction[2][12];
        p8 named = 0;
        bipolar handle;
        socket_address_internet self = {0};
        b32 self_size = sizeof self;
        p64 start;

        for (positive at = 0; at < 2; at++)
                if (net_resolve_v4_apart(discovery_stun_servers[at].name,
                                         server + at, 1) == DNS_OK)
                        named |= (p8)(1u << at);
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
        discovery_stun_wait(handle, found, named, transaction, start + 500000000ull);
        if ((found->answered & named) != named)
        {
                discovery_stun_send(handle, found, server, named, transaction);
                discovery_stun_wait(handle, found, named, transaction,
                                     start + 1000000000ull);
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
        bytes, believed only as an address. */
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
        discovery_trace_said said;
        http_buffer body = {0};

        (void)context;
        memory_zero(address_of said, sizeof said);
        said.status = http_run((string_address)DISCOVERY_TRACE_URL,
                               address_of discovery_trace_manners, true, -1,
                               address_of body, null, null, null);
        said.found = !said.status &&
                     discovery_trace_address(body.bytes, body.used, said.address);
        byte_store_release(address_of body);
        return system_write_all((positive)answer, address_of said, sizeof said) ==
               sizeof said;
}

static fn discovery_https(discovery_found address_to found)
{
        byte_store got = {0};
        discovery_trace_said said;
        bipolar ran;

        discovery_sent++;
        ran = net_apart(discovery_trace_task, null, -1, address_of got,
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
                        found->https_when = discovery_now();
                }
        }
        byte_store_release(address_of got);
}
#endif

//      What was kept, or zeros when nothing whole was.
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
}

/*      What a gather that learned nothing new keeps of the ones before: the
        last STUN answer whole (which servers answered, what each saw, the
        NAT told from it and when) and the last trace's address and when,
        however many gathers ago they came. */
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
                found->stun_when = before->stun_when;
        }
        if (found->https != DISCOVERY_HTTPS_ANSWERED && before->https_when)
        {
                memory_copy(found->trace, before->trace, 16);
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

/*      The lookups the settings allow, each in about a second, into a
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
                discovery_stun(found, here);
        if ((settings->ask & DISCOVERY_ASK_HTTPS) && found->udp != DISCOVERY_UDP_OUT)
                discovery_https(found);
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

/*      The proxy a program here is told to use, as host:port with any
        user and password left out, or none. */
static p8 discovery_proxy(p8 address_to text, positive room)
{
        static const string_address names[] = {"HTTPS_PROXY", "https_proxy",
                                               "ALL_PROXY", "all_proxy"};
        string_address value = null;
        positive at = 0, length;

        text[0] = end;
        for (positive name = 0; !value && name < array_count(names); name++)
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
                return (string_address) "cloudflare.com did not resolve";
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

/*      The page, each line a key and its value, prefix before each line
        (the doctor's indent, or nothing). Every value is a number, one of the
        tables' words or an interface's name the kernel holds. */
static fn discovery_report(writer say, const discovery_settings address_to settings,
                             const discovery_found address_to found,
                             discovery_here address_to here, string_address prefix)
{
        p8 text[48];
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

        if (found->answered)
        {
                discovery_address_text(found->mapped[first], text);
                discovery_when(found->stun_when, when);
                string_format(say, "%spublic ipv4 %s:%p by STUN, %s\n", prefix,
                              (string_address)text, (positive)found->mapped_port[first],
                              (string_address)when);
        }
        else if (found->https_when && discovery_mapped(found->trace))
        {
                discovery_address_text(found->trace, text);
                discovery_when(found->https_when, when);
                string_format(say, "%spublic ipv4 %s by HTTPS, %s\n", prefix,
                              (string_address)text, (string_address)when);
        }
        else
                string_format(say, "%spublic ipv4 not known\n", prefix);

        if (found->https_when && !discovery_mapped(found->trace))
                v6 = found->trace;
        for (positive at = 0; !v6 && at < here->owns; at++)
                if (!discovery_mapped(here->own[at].address) &&
                    here->own[at].scope == DISCOVERY_SCOPE_GLOBAL)
                {
                        v6 = here->own[at].address;
                        v6_where = discovery_interface_of(here, here->own[at].index);
                }
        if (v6 && v6 != found->trace)
        {
                discovery_address_text(v6, text);
                string_format(say, "%spublic ipv6 %s, %s's own global address, now\n",
                              prefix, (string_address)text,
                              v6_where ? (string_address)v6_where->name : (string_address)"an interface");
        }
        else if (v6)
        {
                discovery_address_text(v6, text);
                discovery_when(found->https_when, when);
                string_format(say, "%spublic ipv6 %s by HTTPS, %s\n", prefix,
                              (string_address)text, (string_address)when);
        }
        else
                string_format(say, "%spublic ipv6 none: no interface has a global "
                                   "IPv6 address\n", prefix);

        discovery_when(found->asked, ago);
        if (!found->asked)
                string_format(say, "%sudp out not known: nothing looked yet "
                                   "(moonwater network discovery refresh)\n", prefix);
        else if (found->udp == DISCOVERY_UDP_OUT)
                string_format(say, "%sudp out yes, %s\n", prefix, (string_address)ago);
        else if (found->udp == DISCOVERY_UDP_BLOCKED)
                string_format(say, "%sudp out blocked: no STUN server answered in "
                                   "a second, %s\n", prefix, (string_address)ago);
        else if (found->udp == DISCOVERY_UDP_NO_NAME)
                string_format(say, "%sudp out not known: the STUN servers' names "
                                   "were not found, %s\n", prefix, (string_address)ago);
        else
                string_format(say, "%sudp out not asked: %s, %s\n", prefix,
                              settings->switched[0] ? "Public address discovery (STUN) is off"
                                                    : "discovery is off",
                              (string_address)ago);

        if (found->udp == DISCOVERY_UDP_NOT_ASKED && !found->answered)
                string_format(say, "%snat not known: STUN was not asked\n", prefix);
        else
                string_format(say, "%snat %s\n", prefix,
                              (string_address)discovery_nat_names[found->nat]);

        if (found->https == DISCOVERY_HTTPS_ANSWERED)
                string_format(say, "%shttps answered, %s\n", prefix, (string_address)ago);
        else if (found->https == DISCOVERY_HTTPS_FAILED)
                string_format(say, "%shttps failed: %s (status %b), %s\n", prefix,
                              discovery_https_reason(found->https_status),
                              (bipolar)found->https_status, (string_address)ago);
        else if (found->udp == DISCOVERY_UDP_OUT)
                string_format(say, "%shttps not asked: UDP gets out\n", prefix);
        else
                string_format(say, "%shttps not asked\n", prefix);

        if (discovery_proxy(proxy, sizeof proxy))
                string_format(say, "%sproxy %s, named by the environment; "
                                   "nothing goes through it yet\n",
                              prefix, (string_address)proxy);
        else
                string_format(say, "%sproxy none\n", prefix);

        string_format(say, "%saddresses", prefix);
        for (positive at = 0; at < here->owns; at++)
        {
                discovery_local address_to mine = here->own + at;
                discovery_interface address_to where =
                    discovery_interface_of(here, mine->index);

                if (mine->scope == DISCOVERY_SCOPE_LOOPBACK)
                        continue;
                discovery_address_text(mine->address, text);
                string_format(say, "%s %s %s %s/%p %s", shown++ ? ";" : "",
                              where ? (string_address)where->name : (string_address)"?",
                              (string_address)discovery_kind_names[where ? where->kind : 0],
                              (string_address)text, (positive)mine->prefix,
                              (string_address)discovery_scope_names[mine->scope]);
        }
        string_format(say, "%s, now\n", shown ? "" : " none but loopback");

        string_format(say, "%slearned %s\n", prefix, (string_address)ago);
}

#endif // NET_DISCOVERY_INCLUDED
