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

## The order it lands in

1. the path report, last-known places, interfaces, keepalives
2. IPv6, link-local too
3. TCP 443 and the proxy's tunnel
4. PCP, NAT-PMP, UPnP
5. STUN
6. the Worker's rendezvous and its WebSocket relay
7. racing and migration
8. hole punching
9. DNS TXT and Nostr
10. the DHT, which needs Ed25519 in lib.c

## The rule for a path

A path is Moonwater's own code. It asks nothing the person must install, sign up for, give a key to or pay. What it talks to is one of these: a free public service that asks no key (STUN, DNS over HTTPS, the Mainline DHT, a Nostr relay, ntfy.sh), the owner's own Worker, or the person's own machines. That rules out the Tor and cloudflared programs, Tailscale, ZeroTier and WARP clients, TURN servers that hand out credentials, and cloud machines that need an account. It also rules out Tailscale's DERP servers used without its client, public test MQTT brokers, and Teredo.

## Next, after the order above

1. **Port prediction.** Against a NAT that maps each destination to its own port, both sides open many ports at once (about 256), so the birthday bound finds a pair. No third party.
2. **IPv6 pinholes.** A global IPv6 address has no NAT, only a stateful firewall. Both sides send first, and PCP or UPnP-IGD's WANIPv6FirewallControl opens a pinhole where the router offers one.
3. **The group's own relays.** A machine of the group that can be reached relays for the others, under the group's keys. With one such machine, nothing outside the group carries the link.
4. **Signalling over ntfy.sh**, beside Nostr: a sealed record posted to a topic named by the group's hash.
5. **Later, spoken natively, never by installing their code:**
   - HyperDHT (hole punching, and relaying through its nodes);
   - libp2p's circuit relay v2 (limited relays, enough to coordinate a punch);
   - Yggdrasil's public peers.

The gathering half (local addresses and kinds, STUN, the HTTPS fallback,
the proxy, the switches and the report) is the first to land.
