# Connective: machines that find each other with no port forwarded

`src/waterlink/connective.c` is the part of Moonwater that learns how this
machine can be reached and how it can reach out. It knows nothing of
waterlink: it takes switches and hands back what it found, so `moonwater ssh`,
the update fetcher and the time sync can ask it the same way the link does.

It follows the algorithm of ICE (RFC 8445). The code is Moonwater's own.

## The steps

1. **Gather** the candidates, each with where it came from:
   - the places a peer was last heard at (`/root/link.places`);
   - every local address, IPv4 and IPv6, global, unique local and
     link-local, per interface, with the interface's kind: ethernet, wifi,
     a VPN (wg, tailscale, zerotier, tun) or other;
   - ports the router maps when asked: PCP (RFC 6887), NAT-PMP (RFC 6886),
     UPnP-IGD;
   - the public address, by a STUN binding (RFC 8489) to
     stun.cloudflare.com:3478 and stun.l.google.com:19302. The same mapping
     from both is a NAT that maps by endpoint, which a hole can be punched
     through; two mappings is one that cannot be, without help;
   - where UDP does not get out, the public address over HTTPS
     (`https://cloudflare.com/cdn-cgi/trace`, read in a confined child as the
     zone question reads it);
   - TCP listeners, and an HTTP proxy (`HTTPS_PROXY`, by CONNECT).
2. **Publish and fetch** candidate records, sealed with the group's secret
   and named by a hash of it, through every rendezvous at once: LAN
   discovery, an owner-deployed Cloudflare Worker over HTTPS (signed,
   rate-limited requests), DNS TXT by DNS over HTTPS, the Mainline DHT
   (BEP 44) and Nostr. A rendezvous carries only sealed records, so none of
   them learns more than that a record was put there.
3. **Race** the candidate pairs (Happy Eyeballs, RFC 8305), punch holes in
   UDP and TCP, keep a path open with a keepalive every 25 seconds, and
   move a session to a better path when one appears.
4. **Carry** the link over UDP, TCP on 443, the proxy's tunnel, a WebSocket
   over HTTPS, or the sound card's modem.
5. **Relay** last: the Worker's Durable Object relays small traffic over a
   WebSocket; another Moonwater machine of the group that can be reached (a
   public IPv4 or IPv6 address, or a mapped port) relays for the rest.
6. **Report** what it found in `moonwater link doctor` and in Settings.

## Discovery switches

Every lookup that sends a packet off the machine has a switch of its own,
named for what it discovers, and `moonwater link discovery off` turns them
all off at once. Off means no packet leaves for it. Each is on by default.

    moonwater link discovery [on|off]          every lookup below
    moonwater link discovery stun [on|off]     Public address discovery (STUN)
    moonwater link discovery https [on|off]    Public address discovery (HTTPS)
    moonwater link discovery refresh           look again now

The servers a lookup asks learn this machine's public address; that is
written in `.github/SECURITY.md`.

## The rule for a path

A path is Moonwater's own code. It asks nothing the person must install, sign up for, give a key to or pay. What it talks to is one of these: a free public service that asks no key (STUN, DNS over HTTPS, the Mainline DHT, a Nostr relay, ntfy.sh), the owner's own Worker, or the person's own machines. That rules out the Tor and cloudflared programs, Tailscale, ZeroTier and WARP clients, TURN servers that hand out credentials, and cloud machines that need an account. It also rules out Tailscale's DERP servers used without its client, public test MQTT brokers, and Teredo.

## The plan

Every item is Moonwater's own code under the rule above, has its
discovery switch where it sends a packet off the machine, and shows what
it found in `moonwater link doctor` and in Settings' Network and
Waterlink rows. Sizes: S is a day of one agent or less, M two or three, L
more. An item is proven by rows in CHECK_waterlink (pure: the wire, the
parse, the decision) and, for what needs a real network, on a real
machine; this sandbox has no UDP out and no inbound connection.

### Landed

- **G1 Gathering** (ac253e74): interfaces and their kinds, IPv4 and IPv6
  with scope, UDP out, STUN's public address and NAT mapping, the HTTPS
  trace where UDP is blocked, the proxy, the switches, the report.
- **G2 Name servers** (in progress): 1.1.1.1 and 1.0.0.1 first, the
  network's servers as the fallback, the network's own names asked of the
  network first, all of it shown in Settings.

### Phase 1: direct paths, no one in the middle

- **D1 Last-known places and keepalives** (S). Try `/root/link.places`
  first; a keepalive every 25 s holds a NAT's mapping open.
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
  Moonwater's own name, the way a printer advertises itself.
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

### Later

Spoken natively, never by installing their code: HyperDHT (hole punching,
and relaying through its nodes), libp2p's circuit relay v2 (limited
relays, enough to coordinate a punch), and Yggdrasil's public peers.

### Suggested order

D1, D2, D4, D5, C2 and D6 are small and cover the most common gaps. Then
D3, R1 and R2 with its mailbox, C1 and C3, Y1, C4, Y2. Then the rest of
phase 2, phases 5 and 6 as hardware allows, and the "Later" list.
