# Network security review status

This is a gap register, not a claim that the network stack is “bulletproof.”
It exists to stop security work becoming a loop of adding another isolated
malformed packet. A pass counts as new evidence only when it closes a named
threat family, demonstrates that the test fails without the fix, or removes a
gap below.

## Current assessment

The stack has strong hand-written boundary checks and unusually broad
deterministic regression coverage. Local lanes now exist for coverage-guided
TLS fuzzing under ASan/UBSan (`sh test/run fuzz`), MSan over hosted parser
lifts (`sh test/run msan`), resource-exhaustion and mid-path fault sweeps, and
`http.client`, curl and GNU wget oracles for HTTP framing and delivery. It is **not yet release-grade
evidence for memory safety or parser completeness** because those lanes are
not mandatory (the CI `security` job is parked, `workflow_dispatch` only),
fuzzing covers TLS, DER, DHCP, SNTP, Wi-Fi, DNS, netlink, crypto and the HTTP section (a default-tier model) but not continuously.

| Area | Evidence present | Important remaining gap |
| --- | --- | --- |
| Netlink | sender PID, sequence, length/alignment, multipart and truncation checks | persistent fuzzing of nested attributes; mandatory namespace runs |
| DNS | exact question/ID binding, compression loops, full RR framing, UDP truncation to TCP | coverage-guided compression/name fuzzing; independent packet oracle; DNSSEC is out of scope |
| TLS records/handshake | record and handshake fragmentation, transcript/Finished, AEAD limits, state ordering, `tls_hs_fuzz` libFuzzer target | record-layer fuzzing; mandatory fuzz budget in CI |
| X.509 | strict DER and generated-chain policy matrix against OpenSSL; `tls_der_fuzz` / `tls_verify_fuzz` libFuzzer targets; DNS-name and IP matching against OpenSSL's own check (`tls_hostnames`); Wycheproof's vectors under the crypto (`crypto_vectors --wycheproof`); wget's status 5 and wording for every refused chain | a second independent path validator; name-constraints breadth; Mozilla `distrust-after` dates and per-anchor constraints (the anchor table is key-only); revocation |
| HTTP/URL | sink-side request validation, framing conflicts, 204/205/304 as wget and curl take them (the tight tier, `MOONWATER_STRICT` 2, holds them to RFC 9110), split-point and chunk/trailer checks, `http.client` and curl framing oracles, HTTPS downgrade harness, GNU wget and curl as live oracles for byte-at-a-time, split, FIN, RST, 1xx-storm and pipelined-bytes schedules (`wget_mutation`) and for request lines, redirects, saved files and exit statuses against scripted servers (`wget_hostile`), URL splitting and Location resolution against `urllib` (`http_urls`) | coverage-guided framing fuzzing that models the tight tier; slow-stream scheduling across TLS and redirects; open decision: the default refuses an identical duplicate `Content-Length` (and a list `3, 3`), an obs-fold line and a control byte in the reason phrase, all of which wget and curl accept |
| DHCPv4 | peer/xid/MAC binding, option framing/overload, state cross-product, entropy faults | coverage-guided option-stream fuzzing; mandatory namespace/netem retransmission runs |
| SNTP | nonce and peer binding, ancillary timestamp parsing, arithmetic and selection checks | adversarial scheduling/netem as a mandatory lane; era-boundary integration tests |

### TLS trust policy (decided, not gaps)

- **Revocation and transparency: none.** No OCSP, CRL or CRLite, and no
  Certificate Transparency check, as with the default `wget` and `curl`; the
  client verifies a chain to a Mozilla root and nothing more. A revoked
  certificate stays valid until it expires.
- **Names: SAN only.** A leaf without a matching subjectAltName is refused; the
  common-name fallback that GnuTLS-based `wget` still has is deliberately not
  implemented (Chrome has none). A wildcard's star is one whole label, never on
  a public suffix, and may stand for an underscore label.
- **Anchors are keys.** `anchors.inc` holds 120 Mozilla server roots as keys
  (regenerated and cross-checked with `--harness anchors --bundle`, last
  against ca-certificates-mozilla 3.129, one label changed, no root added or
  dropped). Anchor validity dates and Mozilla's `distrust-after` dates are not
  enforced: Izenpe.com is in the table and carries a server distrust-after of
  2026-04-15 upstream. None of the 120 carries NameConstraints or expires
  within two years. A tier that enforces the dates would need a per-anchor date.
- **The clock is trusted.** A certificate outside its dates is refused; a clock
  decades wrong (before the first SNTP answer) makes every server look not yet
  activated, and wget now says so and names the clock. SNTP is unauthenticated,
  so an on-path attacker who moves the clock moves certificate validity with it.
- **Not in ring 0.** No `crypto_`, `tls_` or X.509 code is in the include graph
  of `src/moonwater/core.c`; `net.c` is compiled into userspace programs only.

The unauthenticated-protocol limits in `SECURITY.md` remain fundamental: an
on-path attacker can forge DHCPv4, ordinary DNS, and SNTP. Parser hardening
does not convert those protocols into authenticated ones.

## Priority queue

### P0 — evidence needed before a high-assurance claim

1. Extend persistent fuzzing beyond TLS (DER, certificate lists and
   handshake fragmentation are covered by `sh test/run fuzz`) to DNS names/RRs,
   HTTP response framing/chunks, DHCP option streams, and SNTP control
   messages. Seed them from the unit corpus and run ASan+UBSan.
2. Make x86-64 ASan+UBSan and native namespace/netem runs required CI jobs.
   `MOONWATER_FUZZ_REPORT=… sh test/run fuzz` already records compiler and sanitizer
   versions, seed counts, budgets and exits; it has to run on every release,
   not by hand.
3. Keep MSan (`sh test/run msan`) separate from UBSan and widen it from hosted
   lifts toward a full freestanding `CHECK_net`; today it is exercised on
   aarch64 Linux clang only.
4. Response framing has `http.client` and curl beside the written matrix, and
   `wget_mutation` runs GNU wget and curl over the same delivery schedules;
   keep pinning Moonwater's policy for every disagreement (`DELIBERATE`,
   `TIGHT_REFUSES`, `STRICTER_THAN_WGET`).

Application layer (HTTP client, wget, URL handling), checked against GNU wget
1.25.0 and curl 8.22.0 with scripted servers (`wget_hostile`): default behaviour
follows them, and what this client refuses that both fetch is deliberate and
named: userinfo in a URL, `%00`/`%0d`/`%0a` in a request target, a backslash
in an authority, a head over 16 KiB, a URL of 2,048 bytes or more, an IDN host
(no IDNA mapping is built, so `http://münchen.de/` is refused, not guessed at),
an HTTPS-to-HTTP redirect, and the saved file's escaped rather than decoded name.
The tight tier (`MOONWATER_STRICT` 2) adds the refusal of a redirect from public
address space to one that is not public. Not built, proposed: a floor under the
clock the certificate dates are judged by (`tls_date_now` takes `time()` as it
is, so a rolled-back clock makes an expired certificate current); no floor is
needed to fail closed on a clock that starts at 1970, since every certificate is
then not yet valid.

### P1 — resource and state-machine assurance

1. Byte, item and recursion ceilings are recorded and hit-tested
   (`SECURITY_TEST_MATRIX.md`); add an explicit CPU-work budget per parser or
   transaction loop.
2. Allocation, `mmap`, descriptor, short-I/O, `EINTR` and deadline faults are
   injected for HTTP, TLS, DNS, DHCP and netlink; add clock-jump faults and
   extend the rest to SNTP.
3. Make ARM64 and RISC-V network lanes required, including sanitizer-capable
   hosted builds where available. “Not run” is not evidence.
4. Deterministic hostile scheduling for TCP/TLS/HTTP exists for plain HTTP
   (`wget_mutation`: one-byte delivery, splits at every grammar boundary,
   early FIN/RST, 1xx storms, bytes arriving with close); long pauses,
   simultaneous timeout and the TLS and redirect legs are still to do.

### P2 — independent and operational assurance

1. Add a second X.509 path-building oracle and a durable corpus of real and
   synthetic certificates, including name constraints and unusual but valid
   chains.
2. Run namespace tests with loss, duplication, reordering, delay, MTU changes,
   stale queued datagrams, and source-address changes at every protocol phase.
3. Add static-analysis and compiler-hardening reports to releases, with every
   suppression reviewed. Static analysis is supporting evidence, not a proof.
4. Commission an independent review after the P0 lanes are reproducible; an
   internal checklist cannot establish its own completeness.

## Definition of done for future fixes

A networking security change is complete only when all of these are recorded:

1. attacker position and violated invariant;
2. reachable call path and concrete malicious input;
3. consequence, including whether it is memory corruption, authentication,
   parser differential, denial of service, or hardening only;
4. smallest fix at the shared trust boundary;
5. family-level procedural test, including valid neighbors;
6. mutation proof that restoring the vulnerable behavior fails that test;
7. resource ceiling and portability implications; and
8. residual risk which the change does not address.

Until the P0 queue is complete, the honest rating is **strong deterministic
unit/integration coverage, incomplete high-assurance evidence**. More isolated
unit cases may still fix real bugs, but they do not move that rating by
themselves.

## Link, lease and kernel network defaults (netlow pass, 2026-09-30)

Measured on a built default image in a KVM guest (`/proc/sys`, `/proc/net`,
`artifacts/.config`), not read from the profiles.

Found sound, and how: the watcher's wake FIFO is `prw-------` root in a
root-owned `0755` `/run/moonwater`, so only root can restart an exchange from
it; a cut exchange's child is killed and reaped, the answer wins a race with
the cut, and a renewal is never cut. The DHCP client asks for options 1, 3 and
6 only and stores every one as a 32-bit address, so there is no hostname,
domain, search list, NTP, MTU, classless route (121, 249) or any text that
reaches a file, and `resolv.conf` is written from two dotted quads. Nothing is
bound after the lease, NTP and the zone fetch (30 s in): `/proc/net/tcp`,
`udp`, `raw`, `packet`, `unix` are empty. The sampled SNTP, DNS and zone-header
paths were re-read against their fuzz targets and stand as before.

Fixed here: a carrier flap on any other link cut every acquisition (it now
waits out a four second hold-off after a cut); a `/32` lease from a router
outside its prefix was rolled back for ever (the route is now marked on-link);
`CONFIG_SYN_COOKIES` was off in `general` (no `tcp_syncookies` at all); and
`sec_hardened` now refuses ICMP redirects, source routes, TIME-WAIT
assassination and io_uring at watcher start.

Open, ranked:

1. No 802.11w. `wifi_rsn_ie` carries no MFPC bit and no IGTK is installed, so a
   deauthentication frame from anyone in radio range ends the association.
   Needs an RSN group-management cipher, `NL80211_ATTR_USE_MFP` and the IGTK
   key data in message 3; the `wifi` lane's radio emulation has no AP to test
   it against, and the box has no hostapd.
2. The DHCP client is a UDP socket, so `rp_filter` 1 or 2 drops the OFFER
   (route to the server does not exist yet) and no lease is ever taken:
   measured, OFFERs sent and no REQUEST. `rp_filter` therefore stays 0 in
   every tier; an `AF_PACKET` receive path would lift that.
3. No ARP probe or gratuitous ARP for a leased address (`arp_notify` is 0,
   `arp_announce` 0, `arp_ignore` 0, `arp_filter` 0): an address in use
   elsewhere is not noticed.
4. Unauthenticated SNTP is accepted into any moment between 2026-09-01 and
   2036-01-01 while the clock is unset, and within 24 hours of it afterwards;
   an on-path attacker at boot chooses the date the certificate checks run
   under. Authenticated time (NTS) is not implemented.
5. Name resolution asks 1.1.1.1 first, in the clear, before the network's own
   resolver; a source port and a 16-bit id are the only protection (the kernel's
   port range 32768-60999, no 0x20 encoding).
6. The general image enables IPv6 with SLAAC and router advertisements
   accepted (`accept_ra` 1, EUI-64 addresses, `accept_redirects` 1) although
   none of the tools here speak it; `modern` turns IPv6 off.
7. Outside the network zone, seen while measuring: `fs.protected_*` 0,
   `kernel.kptr_restrict` 0, `kernel.dmesg_restrict` 0, unprivileged user
   namespaces and io_uring on in the default tier.

## Open issue ledger

The one list of every known, documented security or hardening issue in the
network stack and its consumers that is not yet closed, with who is on it.
Sources are this file, `SECURITY_TEST_MATRIX.md`, the PR 16 body (`## Open /
not covered`), `README.md`, and the earlier passes' ranked lists. An item
leaves this table only when it is fixed (commit in the last column), fixed
behind a tier with the default deliberately 1:1 with wget/curl/bash/dash, or
moved to "Decisions" below with a reason. Status: `open`, `in progress (stream)`
or `closed (sha)`. Streams: closeclock = clock trust, NTS, distrust-after;
closenet = DHCP AF_PACKET, ARP, DNS, IPv6, sysctl defaults; closewifi = PMF,
SAE, wifi storage, link.groups, waterlink floods; closesupply = pinned
downloads, boot disk, argv secrets, terminal fuzz; closeledger = the rest.

| Id | Where | Issue | Status |
| --- | --- | --- | --- |
| L01 | netlow #4; `SECURITY.md` | Unauthenticated SNTP is accepted into 2026-09-01..2036-01-01 while the clock is unset; an on-path attacker at boot picks the date certificate checks run under | in progress (closeclock) |
| L02 | nethttp; TLS trust policy | `tls_date_now` takes `time()` with no floor: a rolled-back clock makes an expired certificate current | in progress (closeclock) |
| L03 | netlow #4; PR 12 deferral | No NTS (authenticated time) | in progress (closeclock) |
| L04 | TLS trust policy; nettls | Mozilla `distrust-after` and per-anchor constraints not enforced (`anchors.inc` is SPKI-only; Izenpe.com carries 2026-04-15 upstream) | in progress (closeclock) |
| L05 | matrix SNTP row | Era-boundary SNTP integration tests (2036 NTP era rollover, 2038) | in progress (closeledger) |
| L06 | netlow #2 | DHCP client is a UDP socket: `rp_filter` 1 or 2 drops the OFFER, so `rp_filter` stays 0 in every tier | in progress (closenet) |
| L07 | netlow #3 | No ARP probe or gratuitous ARP for a leased address (`arp_notify`/`arp_announce`/`arp_ignore`/`arp_filter` 0) | in progress (closenet) |
| L08 | netlow #5 | DNS asks 1.1.1.1 first, in the clear, 16-bit id and kernel source port only, no 0x20 | in progress (closenet) |
| L09 | netlow #6 | General image: IPv6 with SLAAC, RA and redirects accepted though no tool speaks it | in progress (closenet) |
| L10 | netlow #7 | `fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, unprivileged user namespaces and io_uring on in the default tier | in progress (closenet) |
| L11 | a40bfc9d note | DHCP yiaddr/router sanity (127/8, 224/4, router equal to the address) not refused; rogue-server-only | in progress (closenet) |
| L12 | d1bcc3eb note | SYN cookies enabled and read back but not measured under a flood | open (closenet) |
| L13 | netlow #1; nettls | No 802.11w/PMF: `wifi_rsn_ie` has no MFPC bit, no IGTK; a spoofed deauthentication frame ends the association | in progress (closewifi) |
| L14 | README; netlink2 | No SAE: WPA3-only networks cannot be joined | in progress (closewifi) |
| L15 | netlink2 | TKIP group cipher unsupported (by design) | open (closewifi, decision candidate) |
| L16 | netlink2 | `/root/wifi` is rewritten in place (a crash mid-write loses the list); bluetooth add/remove take no radio lock (lost update) | in progress (closewifi) |
| L17 | netlink2 | Group `check` in `/root/link.groups` is a fast salted SHA-256: whoever holds the file tests guesses at full speed | in progress (closewifi) |
| L18 | netlink2 | Live waterlink handshake flood under netem not tested; MSan not run on waterlink | in progress (closewifi) |
| L19 | security-pass-2 #12 | wifi first-message sender unchecked (DoS) | open (closewifi) |
| L20 | security-pass-2 #12 | `moonwater wifi add SSID PASS` puts the password in argv (the `-` stdin form exists) | open (closesupply) |
| L21 | secnet #2; bowl.c | Arch/Debian/ArchARM/RISC-V bootstrap tarballs unpinned and unsigned (TLS to the mirror only) | in progress (closesupply) |
| L22 | secnet #3 | `link join NS SECRET` puts the group secret in argv | in progress (closesupply) |
| L23 | security-pass-2 #1 | Boot takes any attached disk that looks like an install and runs its `main.moonwater.sh` as root (evil-maid); data partition mounted without nodev/nosuid | in progress (closesupply) |
| L24 | security-pass-2 #2 | `SPARK_IOCTL_BIND` GET / `MOONWATER_SCRIPT_GET` readable by anyone while settings GET is CAP_SYS_ADMIN | in progress (closesupply) |
| L25 | security-pass-2 #3, #6 | `edit` draws file bytes and names raw to the terminal; `term.c` REP with a DECSTBM region costs ~70 MB of copying per printk on a 4K console | in progress (closesupply) |
| L26 | secnet #4 | bowl JSON has no fuzz target (input is pinned-digest data) | open (closesupply) |
| L27 | PR 16 body; `STRICTER_THAN_WGET` | Default refuses an identical duplicate `Content-Length`, `Content-Length: 3, 3`, obs-fold lines and a control byte in the reason phrase; wget 1.25.0 and curl 8.22.0 accept all four (a conflicting pair stays refused, as curl does) | in progress (closeledger) |
| L28 | PR 16 body | Truncated head (FIN inside the status line or a header): wget exits 0 with nothing written, this client fails | in progress (closeledger; decision) |
| L29 | PR 16 body; matrix | The tight tier is not fuzzed: `http_fuzz` models the default only; `http_fuzz` was red at 90aefe55 (205 seed, fixed 25eea8af) | in progress (closeledger) |
| L30 | matrix; secnet #1 | `tls_parse_cert` and its ~75 raw index reads are guarded by hand, fuzzed and judged sound, but not on `byte_reader` | open (closeledger) |
| L31 | P0 #1; matrix DHCP row | Coverage-guided option-stream fuzzing for DHCP (committed corpus) | in progress (closeledger) |
| L32 | matrix TLS row | Record-layer fuzzing for TLS (committed corpus) | in progress (closeledger) |
| L33 | P2 #1; matrix X.509 row | A second independent X.509 path-validator oracle; name-constraints breadth | in progress (closeledger) |
| L34 | P0 #2; matrix DHCP/SNTP rows | Native namespace/netem retransmission and adversarial-scheduling runs are not a mandatory lane | in progress (closeledger) |
| L35 | P0 #2; header | The fuzz budget and the net lane are not mandatory in CI (`ci.yml` is `workflow_dispatch` only) | in progress (closeledger) |
| L36 | P0 #3 | MSan (`sh test/run msan`) is exercised on aarch64 Linux clang only and on hosted lifts, not a freestanding `CHECK_net` | open (closeledger) |
| L37 | P1 #1, #2, #4 | No explicit CPU-work budget per parser loop; clock-jump faults and SNTP fault injection; long pauses, simultaneous timeout and the TLS and redirect legs of hostile scheduling | open (closeledger) |
| L38 | P1 #3 | ARM64 and RISC-V network lanes are not required anywhere; sanitizer-capable builds there are not run | open (closeledger) |
| L39 | P2 #3, #4 | Static-analysis reports with suppressions reviewed; an independent review after the P0 lanes are reproducible | open (closeledger; user decision for the review) |
| L40 | kit lane | `sh test/run kit` is 39 of 41 on this branch and on main (firmware line pins, performance map BENCH sections); not a network defect, carried for honesty | open (main's, not this branch's) |

### Decisions (closed by decision, reasons recorded; the user can override)

| Id | Decision | Reason |
| --- | --- | --- |
| D01 | No OCSP, CRL, CRLite or Certificate Transparency | Same as default wget and curl; a revoked certificate stays valid until it expires |
| D02 | No CN fallback | GnuTLS-based wget has one, Chrome none; refusing is deliberate |
| D03 | DNSSEC is out of scope | The resolver is a stub that trusts the network's DNS; validating would need a trust-anchor and time story this image does not have while L01-L03 stand. Revisit after closeclock lands |
| D04 | URL refusals kept: userinfo, `%00`/`%0d`/`%0a` in a target, a backslash in an authority, a URL of 2,048 bytes or more, an IDN host (no IDNA), HTTPS-to-HTTP redirect, head over 16 KiB | Named and pinned in `wget_hostile` and `http_urls`; stack buffers bound them |
| D05 | Saved file name is the escaped, not the decoded, name | Decoding needs `%2f`/`%00`/`%1b` re-escaping; safer as is |
| D06 | A body dripped one byte per 29 s runs forever; no Content-Length pre-check for disk fill | Each read has its own 30 s timeout as GNU wget; GNU has no pre-check either |
| D07 | HEAD not exercised | The client only builds GET |
| D08 | "Guest is root, not a wall": root is not hardened against itself | Documented policy in `SECURITY.md` |
