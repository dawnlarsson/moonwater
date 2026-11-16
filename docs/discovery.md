# Discovery: machines that find each other with no port forwarded

`src/net/discovery.c` is the module of Moonwater's network stack that learns
how this machine can be reached and how it can reach out. It knows nothing
of waterlink: it reads the person's switches and hands back what it found,
so `moonwater ssh`, the update fetcher and the time sync can ask it the same
way the link does. `moonwater network` is its verb, and bare it says the
network as one page: each interface, the name servers in their order, and
what discovery may look up. It looks on its own at boot and whenever the
network changes, within the person's switches.

It follows the algorithm of ICE (RFC 8445). The code is Moonwater's own.

## The steps

1. **Gather** the candidates, each with where it came from:
   - the places a peer was last heard at (`/root/link.places`);
   - every local address, IPv4 and IPv6, global, unique local and
     link-local, per interface, with the interface's kind: ethernet, wifi,
     a VPN (wg, tailscale, zerotier, tun) or other;
   - ports the router maps when asked: PCP (RFC 6887), NAT-PMP (RFC 6886),
     UPnP-IGD;
   - the public address, by a STUN binding (RFC 8489) to the first two
     servers of the list that resolve (stun.cloudflare.com:3478 and
     stun.l.google.com:19302 by default). The same mapping from both is a
     NAT that maps by endpoint, which a hole can be punched through; two
     mappings is one that cannot be, without help;
   - where UDP does not get out, the public address over HTTPS (the trace
     at the URL kept, `https://cloudflare.com/cdn-cgi/trace` by default, read
     in a confined child as the zone question reads it);
   - TCP listeners, and an HTTP proxy (`HTTPS_PROXY`, by CONNECT).
2. **Publish and fetch** candidate records, sealed with the group's secret
   and named by a hash of it, through every rendezvous at once: LAN
   discovery, an owner-deployed Cloudflare Worker over HTTPS (signed,
   rate-limited requests), DNS TXT by DNS over HTTPS, the Mainline DHT
   (BEP 44) and Nostr. A rendezvous carries only sealed records, so none of
   them learns more than that a record was put there.
3. **Race** the candidate pairs (Happy Eyeballs, RFC 8305), punch holes in
   UDP and TCP, keep a path open with a keepalive (`network keepalive`,
   every 10 seconds of quiet by default), and
   move a session to a better path when one appears.
4. **Carry** the link over UDP, TCP on 443, the proxy's tunnel, a WebSocket
   over HTTPS, or the sound card's modem.
5. **Relay** last: the Worker's Durable Object relays small traffic over a
   WebSocket; another Moonwater machine of the group that can be reached (a
   public IPv4 or IPv6 address, or a mapped port) relays for the rest.
6. **Report** what it found in `moonwater network discovery`, after
   `moonwater link doctor`'s checks, and in Settings.

## Discovery switches and settings

Every lookup that sends a packet off the machine has a switch of its own,
named for what it discovers, and so do the local network's announcing and
asking. `moonwater network discovery off` turns them all off at once. Off
means no packet leaves for it: a gather counts every datagram and
connection it opens (`discovery_sent`), and the local network's socket
refuses a packet whose switch is off before it counts or sends it
(`discovery_lan_allowed`: a response needs LAN announce, a question LAN
listen). Each is on by default, as it was before it could be turned off.

    moonwater network discovery [on|off]          every switch below
    moonwater network discovery stun [on|off]     Public address discovery (STUN)
    moonwater network discovery https [on|off]    Public address discovery (HTTPS)
    moonwater network discovery announce [on|off] LAN announce (mDNS)
    moonwater network discovery listen [on|off]   LAN listen (mDNS), and a pairing code's sweep
    moonwater network discovery refresh           look again now

Turning a switch on is a deliberate act: Settings takes it only from a key
or a press of a device plugged into the machine, never from uinput, uhid,
the on-screen keyboard or the headless pipe (`deliberate` in
`src/sh/registry.c`, as the SSH server's switch is). Turning one off is
never refused.

Nothing a lookup asks is fixed in the code. Each setting has its verb, its
Settings row in the Network group, and a default that is what the code did
before it could be changed:

    moonwater network discovery stun servers [NAME[:PORT]...|add NAME[:PORT]|remove NAME[:PORT]|reset]
                                                  stun.cloudflare.com:3478 stun.l.google.com:19302
    moonwater network discovery stun timeout [MS|default]       1000
    moonwater network discovery https url [URL|default]         https://cloudflare.com/cdn-cgi/trace
    moonwater network discovery https timeout [SECONDS|default] 3
    moonwater network proxy [on|off]                            on: HTTPS_PROXY is taken
    moonwater network keepalive [SECONDS|default]               10, a quiet link's keepalive
    moonwater dns default [ADDRESS...|reset]                    1.1.1.1 1.0.0.1
    moonwater dns internal [on|off]                             on
    moonwater dns internal suffixes [SUFFIX...|default]         .local .home.arpa .internal .lan .localdomain .home

The servers a lookup asks learn this machine's public address; that is
written in `.github/SECURITY.md`.

## When it looks

A gather is made when asked (`network discovery refresh`, Settings'
Discover now, `link doctor`), and by the network's watcher on its own: as
it starts at boot, at a lease that is new or has moved, and when a link
comes or goes (at most once in 30 seconds for those). The watcher's gather
is a child of a child, so neither the lease nor the watcher waits on it,
and each lookup ends within its time limit. With discovery off, or both its
lookups off, nothing is started (`discovery_auto_wanted`).

## The lines

`moonwater network discovery` says a line a key, and the line a Settings
row reads holds the value alone: `public ipv4 203.0.113.7`, `nat
endpoint-independent`, `learned 2m ago`. How a value was learned and when
are lines of their own after it (`public ipv4 how STUN, port 40000, as
stun.cloudflare.com:3478 saw it`, `public ipv4 when 2m ago`), so the command
line says as much as it did. `moonwater dns` is the same: `dns order`,
then `dns order from` with each server's source, `dns internal on`, its
suffixes, its server and how.

Settings finds a value by its key at a line's start or after a blank or a
semicolon, the first line that has it. Each key is said on its own line
before any other line holds it, and an interface's name, which the kernel
lets hold any byte but a slash, a colon and a blank, is said quoted and
escaped (`"x\x3bnat"`), so a name cannot carry another row's key or end its
line. The STUN servers and the URL are held by their parsers to bytes that
cannot either.

## The rule for a path

A path is Moonwater's own code. It asks nothing the person must install, sign up for, give a key to or pay. What it talks to is one of these: a free public service that asks no key (STUN, DNS over HTTPS, the Mainline DHT, a Nostr relay, ntfy.sh), the owner's own Worker, or the person's own machines. That rules out the Tor and cloudflared programs, Tailscale, ZeroTier and WARP clients, TURN servers that hand out credentials, and cloud machines that need an account. It also rules out Tailscale's DERP servers used without its client, public test MQTT brokers, and Teredo.

## The plan

Every item is Moonwater's own code under the rule above, has its
discovery switch where it sends a packet off the machine, and shows what
it found in `moonwater network discovery`, `moonwater link doctor` and
Settings' Network and Waterlink rows. Sizes: S is a day of one agent or less, M two or three, L
more. An item is proven by rows in CHECK_waterlink (pure: the wire, the
parse, the decision) and, for what needs a real network, on a real
machine; this sandbox has no UDP out and no inbound connection.

### Landed

- **G1 Gathering** (ac253e74): interfaces and their kinds, IPv4 and IPv6
  with scope, UDP out, STUN's public address and NAT mapping, the HTTPS
  trace where UDP is blocked, the proxy, the switches, the report.
- **G2 Name servers** (77e24b44): 1.1.1.1 and 1.0.0.1 first, the
  network's servers as the fallback, the network's own names asked of the
  network first, all of it shown in Settings.
- **G3 Every setting the person's**: the STUN servers, the trace's URL,
  each lookup's time limit, the proxy, the keepalive, the DNS default list,
  internal-name routing and its suffixes, and LAN announce and LAN listen
  as switches of their own, each with its verb and Settings row and today's
  behaviour as its default; turning a lookup on is deliberate; the lines
  Settings reads hold their values alone, with how and when on lines of
  their own, and an interface's name cannot speak for another row; a
  gather at boot and when the network changes.

### Phase 1: direct paths, no one in the middle

- **D1 Last-known places and keepalives** (S). Try `/root/link.places`
  first; a keepalive after `network keepalive` seconds of quiet (10 by
  default) holds a NAT's mapping open.
- **D2 IPv6 direct** (S). Global and link-local addresses as candidates;
  both sides send first through a stateful firewall.
- **D3 Router mappings** (M). PCP (RFC 6887), NAT-PMP (RFC 6886) and
  UPnP-IGD, IPv4 ports and IPv6 pinholes (PCP, WANIPv6FirewallControl),
  and PCP to a carrier's NAT where the ISP answers it. Rows: each
  request and answer against the RFCs' layouts.
- **D4 Hairpinning** (S). Two machines with the same public address try
  each other's local addresses first, then the public one through the
  router.
- **D5 NAT64** (S). On an IPv6-only network, find the translation prefix
  (RFC 7050, `ipv4only.arpa`; the well-known 64:ff9b::/96 by default) and
  make an IPv6 address for an IPv4 peer (RFC 6052). Rows: the RFC 6052
  examples.
- **D6 Captive portals** (S). Recognise one (RFC 8910's DHCP option 114 and
  RFC 8908's API, or a plain HTTP probe answered with a redirect) and say
  so in the report and in Settings; nothing tries to get past it.

### Phase 2: finding each other

Every rendezvous carries only sealed records, named by a hash of the
group's secret.

- **R1 LAN discovery** (S). mDNS and DNS-SD as `_moonwater._udp`, under
  Moonwater's own name, the way a printer advertises itself. The records,
  the socket and the confined reader are the module's already
  (`discovery_mdns_service`), and waterlink's groups announce
  `_waterlink._udp` through them; what is left is the service of
  Moonwater's own.
- **R1b mDNS over IPv6** (S). The same records on ff02::fb, port 5353,
  joined on every interface with a link-local address and read at hop
  limit 255, so a network with no IPv4 finds its machines. Rows: the join
  and the hop-limit refusal against built control messages.
- **R6 WS-Discovery** (S-M, optional). UDP 3702 on 239.255.255.250 and
  ff02::c, answering a Probe with a ProbeMatch, so Windows' network list
  shows the box. Under LAN announce; the parser runs apart as mDNS's does.
- **R2 The Worker** (M). The owner's Cloudflare Worker: signed,
  rate-limited requests that only Moonwater can make, records kept with a
  time to live, and a **mailbox** where a sealed message waits until an
  offline peer comes back.
- **R3 DNS TXT** (S). Records read by DNS over HTTPS; the Worker writes
  them in the owner's zone.
- **R4 Nostr and ntfy.sh** (M). A sealed record posted as a note or to a
  topic, and read back from more than one relay.
- **R5 Mainline DHT** (L). BEP 44's mutable items. First Ed25519 in
  lib.c, on its three floors and held by CHECK_verify rows.

### Phase 3: connecting

- **C1 Racing** (M). Candidate pairs raced as RFC 8305 does, the first
  that answers kept, the others closed.
- **C2 Connection reversal** (S). When only one side can accept a
  connection, the rendezvous tells that side to call.
- **C3 Hole punching** (M). UDP by both sides sending at an agreed
  moment; TCP by simultaneous open.
- **C4 Port prediction** (M). Against a NAT that maps each destination to
  its own port, both sides open about 256 ports at once, so the birthday
  bound finds a pair.
- **C5 Port 443** (S). The link's own datagrams on UDP 443 and its stream
  on TCP 443, where a network lets those through and little else. It is
  the link's own protocol on that port, not a disguise.
- **C6 Migration** (M). A session moves to a better path when one
  appears, and back when it goes, without starting again.

### Phase 4: relays

- **Y1 The group's own relays** (M). A machine of the group that can be
  reached relays for the others, under the group's keys.
- **Y2 The Worker's relay** (M). A Durable Object carries small traffic
  over a WebSocket, within the free tier, for Moonwater's signed
  requests only.
- **Y3 Wake-on-LAN** (S). A machine of the group on the same LAN wakes a
  sleeping one before the link tries it.

### Phase 5: with no internet at all

- **N1 Direct cable** (S, with D2 and R1). Ethernet between two machines
  over link-local addresses.
- **N2 USB and Thunderbolt networking** (M). A CDC-NCM gadget on a port
  that can be a device, and Thunderbolt networking: the kernel's
  configuration and the interface brought up when a cable arrives.
- **N3 Its own Wi-Fi** (L). An access point the machine opens, or Wi-Fi
  Direct, by nl80211; Wi-Fi Aware where a driver offers it.
- **N4 Bluetooth** (L). Low Energy for discovery, and PAN for a link.
- **N5 The audio modem** (S). `src/waterlink/modem.c` as a carrier the
  race may use.
- **N6 Tethering** (S). A phone's USB or Bluetooth link recognised by its
  interface's kind, and used when it is the only way out.

### Phase 6: pairing

- **P1 A QR invite** (S to show, L to read). The screen shows a sealed
  invite; reading one needs a camera stack.
- **P2 An invite file** (S). The same sealed invite written to a USB
  stick.
- **P3 NFC** (L). Needs a reader's stack.

### Tunnels: reaching a service of the box from outside

Every tunnel is off until the person turns it on, deliberately, and each
says in `moonwater network` and Settings what it exposes and to whom.

- **T1 The Worker's edge** (M). The owner's Worker carries HTTP and
  WebSocket to a box behind a NAT: a box registers by a signed request
  (its key, a name, a time), the Worker keeps the registration with a time
  to live, rate-limits every name and every source, and relays only to a
  box whose registration verifies.
- **T2 `moonwater network tunnel add NAME PORT`** (S). A local port offered
  through the edge under a name; off by default, turning it on deliberate
  (a key or a press of a device plugged into the machine), `network tunnel
  remove NAME` and `network tunnel off` take it away at once.
- **T3 One of the person's own boxes as the edge** (M). A box of the group
  that can be reached (a public address or a mapped port) is the edge for
  the others, with a certificate of its own from ACME (RFC 8555, HTTP-01 or
  TLS-ALPN-01), so no Worker is needed.
- **T4 An IPv6 pinhole as the direct path** (S, with D3). Where the router
  opens one (PCP, or UPnP's WANIPv6FirewallControl), the port is reached on
  the box's global IPv6 address with no edge in the middle.

### Later

Spoken natively, never by installing their code: HyperDHT (hole punching,
and relaying through its nodes), libp2p's circuit relay v2 (limited
relays, enough to coordinate a punch), and Yggdrasil's public peers.

### Suggested order

D1, D2, D4, D5, C2 and D6 are small and cover the most common gaps. Then
D3, R1, R1b and R2 with its mailbox, C1 and C3, Y1, C4, Y2, then T1 to T4
and R6. Then the rest of
phase 2, phases 5 and 6 as hardware allows, and the "Later" list.
