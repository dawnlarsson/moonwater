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
`http.client`, curl and GNU wget oracles for HTTP framing and delivery, and a
`netem` lane that runs the DHCP and SNTP clients against an adversarial server
in namespaces. Fuzzing covers TLS, DER, DHCP, SNTP, Wi-Fi, DNS, netlink, crypto
and the HTTP section at both `MOONWATER_STRICT` tiers (`http_fuzz`,
`http_fuzz_tight`), each at a bounded budget in the lanes and for ten minutes a
target by hand (clean). The net lane and the fuzz lane run on every push and
pull request that touches the network stack
(`.github/workflows/security.yml`, free on a public repository, 90 minute cap,
no schedule); a nightly long run is proposed below, not enabled. The rating
stays **strong deterministic and differential coverage, not a release-grade
proof of memory safety or parser completeness**: the gaps that remain are
listed in the ledger at the end of this file.

| Area | Evidence present | Important remaining gap |
| --- | --- | --- |
| Netlink | sender PID, sequence, length/alignment, multipart and truncation checks | persistent fuzzing of nested attributes beyond the bounded target |
| DNS | exact question/ID binding, compression loops, full RR framing, UDP truncation to TCP | independent packet oracle; DNSSEC is out of scope (decision D03) |
| TLS records/handshake | record and handshake fragmentation, transcript/Finished, AEAD limits, state ordering, `tls_hs_fuzz` libFuzzer target over the whole record layer and handshake state machine (95 seeds, 10 minutes clean); the fuzz budget runs in CI | a corpus beyond the generated seeds is not kept in the tree (seed files in the tree fail `security_hygiene`; the generators are the corpus) |
| X.509 | strict DER and generated-chain policy matrix against OpenSSL; `tls_der_fuzz` / `tls_verify_fuzz` libFuzzer targets; DNS-name and IP matching against OpenSSL's own check (`tls_hostnames`); Wycheproof's vectors under the crypto (`crypto_vectors --wycheproof`); wget's status 5 and wording for every refused chain; a second independent path validator (Go's `crypto/x509`, every row of `tls_chains`) and name-constraints breadth (range shapes, excluded over permitted, case, label boundary, two CAs, mixed leaf names) | Mozilla `distrust-after` dates and per-anchor constraints (the anchor table is key-only); revocation (decision D01) |
| HTTP/URL | sink-side request validation, framing conflicts, 204/205/304 as wget and curl take them (the tight tier, `MOONWATER_STRICT` 2, holds them to RFC 9110), split-point and chunk/trailer checks, `http.client` and curl framing oracles, HTTPS downgrade harness, GNU wget and curl as live oracles for byte-at-a-time, split, FIN, RST, 1xx-storm and pipelined-bytes schedules (`wget_mutation`) and for request lines, redirects, saved files and exit statuses against scripted servers (`wget_hostile`), URL splitting and Location resolution against `urllib` (`http_urls`) | slow-stream scheduling across TLS and redirects. The default takes what wget and curl both take (an identical duplicate `Content-Length`, `3, 3`, a fold after an ordinary field, any reason-phrase byte but NUL and a bare CR) and the tight tier refuses it; a generated-heads model holds both tiers, `http_fuzz_tight` fuzzes the tight one. A truncated head stays a failure on purpose (decision D09) |
| DHCPv4 | peer/xid/MAC binding, option framing/overload, state cross-product, entropy faults | DHCP under the `netem` lane: clean, tripled, forged, late, dropped and NAKed answers, plain and under loss, duplication and reordering, at rp_filter 0, 1 and 2; `dhcp_fuzz` is the coverage-guided option-stream target (reference model, 33 seeds, 10 minutes clean) | the exchange is slow on a link with a round trip over a quarter second (L41) |
| SNTP and NTS | nonce and peer binding, ancillary timestamp parsing, arithmetic and selection checks; NTS (RFC 8915): the AES-SIV-CMAC-256 AEAD against OpenSSL's and RFC 5297's vectors on three machines, the NTS-KE reply parser and the NTP extension fields under `nts_fuzz`, every reply byte changed, cut or replayed refused against independently built replies, and the client against chrony's NTS server and against key-establishment and time servers that are wrong (`nts`); the clock's floor and `/root/clock.good` in `tls_chains` and `moonwater_cli` | the `netem` lane (forged, tripled, late, dropped answers, two lost requests still answered) and `sntp_era` (the 2036 wrap, 2038 and 2104 at the shipped window and at the window moved to the end of era 1); `nts` needs a chronyd built with NTS (`MOONWATER_CHRONYD`) and is NOT RUN without one | plain NTP remains the fallback and is unauthenticated (`moonwater ntp nts only` refuses it); AEAD_AES_128_GCM_SIV is not offered (D12) |

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
  dropped). Mozilla's server-auth `distrust-after` dates are enforced (04a90815):
  each row of `anchors.inc` carries the date (Izenpe.com, 2026-04-15 23:59:59; the
  Entrust roots that carry 2024-11-30 are not in Arch's bundle) and a chain whose
  leaf's notBefore is later than the date of the root it ends at is refused, by
  default, on both ways a chain ends (as NSS, Firefox and Chrome do; openssl has
  no such concept, so `tls_chains` names the refusals `DELIBERATE`). The file is
  regenerated by `differential.py --harness anchors --bundle B --trust-source T
  --write`, which records both input digests in the header and must reproduce the
  file byte for byte. Anchor validity dates are not enforced; none of the 120
  carries NameConstraints or expires within two years.
- **The clock has a floor, and NTS sets an unset or wrong one.** A clock earlier
  than `MOONWATER_CLOCK_FLOOR` (the date of the source, moved with releases and
  held by a `tls_dates` check to within 120 days of the newest commit) or than
  `/root/clock.good` (the latest time the machine knew for certain) is unset, not
  wrong: certificates are not judged against it, `wget` says so and names
  `moonwater time sync` (status 5), and no NTP answer earlier than the floor is
  taken (2a501362). `clock.good` is a lower bound nothing forged can move: an
  authenticated answer writes its exact time (kept as the wall second it set and
  the boot second, so a later step of the wall clock changes nothing), and with
  none this boot it is the floor the boot began with plus the seconds the machine
  has run (7a5171d8; the first rule raised it a day per write from the wall clock,
  which a forged plain answer could have carried ahead of the truth and so locked
  out the real time). The one connection whose certificate is not judged by the
  clock is the NTS key establishment, until an authenticated answer has been taken
  this boot: it is judged as of the floor, `notBefore` not asked (a short-lived
  certificate was issued after the day the system was built, and an RTC two years
  out reads every certificate expired), as chrony's `nocerttimecheck` does but with
  the floor for `notAfter`. An authenticated answer is believed wherever the window
  (the floor to 2036) puts it, where a pool answer may move a clock by a day
  (two seconds once it is synchronised); on a clock that is set, a step past two
  seconds takes a second NTS server to agree. A plain answer that puts the clock
  further from the NTS time of this boot than a crystal can have drifted (five
  seconds and 1000 ppm) is refused. What remains: a machine with no NTS server
  reachable takes plain NTP at its word between the floor and 2036 (at most 24
  hours once the clock is set, two seconds once it is synchronised), and
  `moonwater ntp nts only` refuses it altogether. On the arm.pi profile
  `CONFIG_RTC_SYSTOHC=y` writes a forged plain time into the RTC, which only a later
  NTS answer corrects. Measured end to end in a KVM guest whose RTC reads
  2000-01-01: NTS to `time.cloudflare.com` set the clock 4.5 s into the boot, the
  first attempt, and `wget https://example.com` then worked.
- **Not in ring 0.** No `crypto_`, `tls_` or X.509 code is in the include graph
  of `src/moonwater/core.c`; `net.c` is compiled into userspace programs only.

The unauthenticated-protocol limits in `SECURITY.md` remain fundamental: an
on-path attacker can forge DHCPv4, ordinary DNS, and plain SNTP (NTS answers are
authenticated). Parser hardening
does not convert those protocols into authenticated ones.

## Priority queue

### P0 — evidence needed before a high-assurance claim

1. Persistent fuzzing: done for TLS (DER, certificate lists, handshake and
   record layer), DNS names and RRs, HTTP response framing and chunks at both
   tiers, DHCP option streams, SNTP replies and control messages, netlink,
   Wi-Fi, crypto and waterlink, seeded from generators (the tree keeps no seed
   files by rule) and run under ASan+UBSan; 10 minutes a target by hand is
   recorded clean for http_fuzz, http_fuzz_tight, dhcp_fuzz, tls_hs_fuzz and
   sntp_fuzz.
2. CI: `.github/workflows/security.yml` runs the net lane (which includes
   the sanitizer fuzz smoke at 5 s a target) and `sh test/run fuzz` with
   `MOONWATER_FUZZ_REPORT`, uploading the report, on every push and pull
   request touching the stack. Native namespace/netem runs are the `netem`
   lane (`sh test/run netem`), NOT RUN where a runner lacks `--map-auto`,
   veth or netem. Proposed, not enabled: a nightly `schedule:` with
   `MOONWATER_FUZZ_SECONDS=600`; it costs nothing on a public repository but
   is the owner's to switch on.
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

1. Second X.509 path-building oracle: done, Go's `crypto/x509` beside OpenSSL
   over every generated chain (`tls_chains`), with the two places wget is
   looser than Go named (directoryName constraints Go cannot evaluate) and the
   one place it is looser than OpenSSL named (OpenSSL holds the subject CN to a
   DNS constraint). The corpus is the generator plus the 642 live chains of
   `x509_corpus` (by hand, needs the network).
2. Namespace tests with loss, duplication, reordering, delay and forged or
   late answers: done for DHCP and SNTP (`netem` lane); MTU changes and
   source-address changes at every protocol phase are still to do.
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

While the ledger below has open rows, the honest rating is **strong
deterministic and differential coverage, incomplete high-assurance
evidence**. More isolated
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

Open, ranked (items 2, 3, 5, 6 and 7 of this list are closed by the closenet
pass below, item 1 by the closewifi pass; 4 belongs to the closeclock stream):

1. ~~No 802.11w.~~ Closed in the closewifi pass below (66b4e01b): management frame
   protection is negotiated and tested against spoofed deauthentication on
   simulated radios with a real access point.
4. (closed by the closeclock stream: the clock's floor, NTS and `clock.good`; see
   the TLS trust policy and L01-L03.)

## Closure pass: DHCP, ARP, DNS, IPv6 and kernel defaults (closenet, 2026-10-01)

Each row was found open in the netlow pass above or in the PR 16 body; each
landed as its own commit with a test that failed before it. Measured in KVM
guests of the built image (two e1000 on taps in a network namespace, a scripted
DHCP/DNS server, QMP `set_link`, lease time from the serial console) unless
said otherwise.

| Item | What changed | Commit | Evidence | Kept / decided |
| --- | --- | --- | --- | --- |
| Lease sanity | `dhcp_lease_usable` refuses an address, router or server identifier in 0/8, 127/8 or 224/3, a router equal to the address, a mask whose prefix reaches 0/8, 127/8 or 224/3, and on a subnet wider than /31 the subnet's first and last addresses; the `/32` lease with an off-link router stays | d54bd70b | a 14 x 13 x 10 grid (1,820 leases) against the written rule in `CHECK_net` | default tier: no real server hands these out |
| DHCP over `rp_filter` | acquisition reads OFFER/ACK/NAK from an `AF_PACKET` `SOCK_DGRAM` socket (classic BPF prefilter attached and locked before the bind, `PACKET_AUXDATA`, full parse in `dhcp_unframe`); the send path and renewals stay UDP | d9bcf0f2 | `CHECK_net` runs the exchange across a veth pair into a second network namespace with `rp_filter` 0, 1 and 2 (red on 1 and 2 before); `dhcp_packets` 50,000 IP/UDP frames with one fault each under ASan/UBSan and three planted mutants; KVM lease 4.12-4.15 s against 4.11-4.17 s (four boots each), and a reply unicast to the offered address is now taken (4.18 s); the `netem` lane's rp_filter 1 and 2 rows (the client as built, in a user namespace, with and without netem loss) are 184 of 184 here and fail on the parent's shell ("nobody offered a lease" after 74.9 s) | `rp_filter` is 2 by default and 1 in `sec_hardened` |
| Kernel defaults | `net_kernel_defaults` at watcher start, before any link is up: IPv4 redirects, secure redirects, source routes, send redirects 0; `rp_filter` 2; IPv6 `accept_ra`, `autoconf`, `accept_redirects` 0 on all, default and every interface present; `tcp_rfc1337`, `tcp_syncookies` 1; `fs.protected_*` 1; `kptr_restrict` 1; `dmesg_restrict` 1; `io_uring_disabled` 1; `sec_hardened` adds `rp_filter` 1, `protected_fifos` and `protected_regular` 2, `kptr_restrict` 2, `io_uring_disabled` 2; `sec_reference` keeps the kernel's own | e12605d7 | a boot-lane row reads eight of them from the booted guest (150 of 152 on the parent's image, 151 of 152 on this one; the other miss is `scenario-miss radio 11 status`, in both); read back in default and hardened guests | see decisions D12-D13 |
| Address conflict | three ARP probes before a leased address is used (`dhcp_probe`, RFC 5227), `DHCPDECLINE` and a ten-second hold-off on a conflict, two announcements after the lease is applied; `arp_ignore` 1, `arp_announce` 2, `arp_filter` 1, `arp_notify` 1 | fe98aa94 | the namespace fixture (confined as the watcher's child is) offers an address its own kernel answers ARP for: declined, then the next free one leased; KVM lease 4.38-4.45 s against 4.16-4.18 s; a server claiming the address saw the probe and the DECLINE and leased the next at 14.5 s | default probe window is about 0.2 s, `sec_hardened` keeps RFC 5227's seconds (lease at 9.3-10.2 s); D14 |
| Exchange cut between REQUEST and ACK | the child reports its REQUEST and its probe down the answer pipe; news after that waits up to 2 s (8 s probing in `sec_hardened`) for the answer; an unacknowledged REQUEST is released (`DHCPRELEASE`) by the child and, when the wait is cut, by the watcher | 1322bdcb | namespace fixture: interim record on the pipe, RELEASE counted by the server; KVM with an ACK 1 s late and a carrier toggle at 4.7 s: lease 5.36 s from the first transaction (6.47 s and a second DISCOVER before); an ACK 3 s late: RELEASE at +2 s; hardened probe window with a toggle at 6.0 s: one DISCOVER, no release | f6556826's interruptibility is unchanged for a cut before the REQUEST |
| DNS hygiene | source port drawn from the CSPRNG over 1024-65535 and bound explicitly; 0x20 case mixing with a retry in typed case for a server that folds it; EDNS0 advertising 1232 bytes with a retry without it on FORMERR/NOTIMP | 9b8d47c3 | 96 queries against a reporting server (mixed case, EDNS0, 94 or more distinct ports, 20 or more below 32768); a folding server and a FORMERR server still answered; a KVM guest's queries showed `MixeD.CaSe.EXAMplE.Com`, EDNS0 and ports 61911, 31443, 25781 | a forged reply that differs from the question only in case can make one query ask again without mixing; it still needs the id and the port |
| Resolver order | DHCP nameserver first, 1.1.1.1 behind it; the first server that gives an answer about the name, NXDOMAIN included, is believed (glibc/musl rule) | 812e37bf | a namespace check with two real servers on 127.0.0.2/3: NXDOMAIN from the first ends the lookup, SERVFAIL goes on | a captive portal that answers every name still answers every name |
| DNS over TLS | `moonwater dns plain|tls|tls-only`; 1.1.1.1, 1.0.0.1, 9.9.9.9 and 149.112.112.112 on 853 with the https certificate checks and the resolver's own address against an iPAddress name; `tls` falls back to UDP, `tls-only` asks nothing else | 054f1f9b | `dns_tls` harness: a shell trusting a generated root, a server on 1.1.1.1 in a namespace answering 192.0.2.53 over TLS and 192.0.2.17 over UDP: ten cases (plain, tls, tls with a leaf for another address, expired, nothing on 853, tls-only with each, a resolver outside the four, a garbage setting), 6 of 10 before; live against Cloudflare and Quad9 from the box | off by default, D13 |
| SYN cookies under a flood | measured, not changed | (this commit) | a spoofed-source SYN flood, 20,000 packets a second for 14 s (280,000 SYNs) at a listener in the guest (accept loop, backlog 128): with `tcp_syncookies` 1 the kernel logged "Sending cookies" and 48 of 48 connections from a host it had not met were answered; with 0, 0 of 6 (each timed out after 2 s) and `ListenDrops` read 278,551 | a peer the kernel has already seen succeed gets through with cookies off too (the last quarter of the SYN backlog is kept for destinations it has proven), so the test host must be one that has not; see the recipe below |

Lanes run on the final tree of this pass, each against the parent's where one
could differ: `net` 514,569 of 514,569 (x86_64, arm64, riscv64 under qemu-user,
tight tier, with the namespace checks named NOT RUN on the emulated two),
`netem` 184 of 184 at `rp_filter` 0, 1 and 2 (the parent's shell fails the
`rp_filter` 1 and 2 rows), `machine`, `cli` (5,974), `waterlink` and `link`
7,276 of 7,276, `bowl` 4,071 of 4,071, `kit` 39 of 41 (the same two as the
parent: firmware line pins and the performance map), `storage_io` with the same
three symlink-staging rows as the parent. On images: `boot` 151 of 152 (the
parent's image 150 of 152, the new row, and `scenario-miss radio 11 status` in
both), `canvas` 71 of 71, `install` 116 of 117 (the parent's image gives the
same), `wifi` 8 of 9 on an `hwsim` image of each tree (the same miss, `watch
never took an access point`). The `wifi` and `boot` lanes feed a console that
loses what it is sent while the machine is busy: with other jobs running, one
`wifi` run gave 3 of 9 and another 2 of 9 on this tree, 8 of 9 alone.

SYN flood recipe (by hand): in a network namespace with two taps, a guest on
one (the scripted server of the lease tests serves DHCP, and an HTTP server
serves a 850 KB static listener that accepts and closes), `wget` the listener
into the guest, start it, then `flood.py 10.9.0.50 9000 14 20000` (a raw
IP_HDRINCL sender with random source addresses and ports) while a second
address of the namespace connects every quarter second and counts. Toggle
`/proc/sys/net/ipv4/tcp_syncookies` in the guest between runs.

Sysctl values of the built images (read in guests): default `all`/`default`
`rp_filter` 2, `accept_redirects` 0, IPv6 `accept_ra` 0 on eth0, `protected_*`
1, `kptr_restrict` 1, `io_uring_disabled` 1, ARP 1/2/1/1; `sec_hardened`
`rp_filter` 1, `protected_fifos` and `protected_regular` 2, `kptr_restrict` 2,
`io_uring_disabled` 2, `accept_source_route` 0. `rp_filter` 1 was leased on
one link (the lease exchange and the route it installs); strict filtering on a
host with two leased links and asymmetric routing is not measured and will
drop what arrives on the interface the route back does not use, which is what
it is for.

## Supply chain, secrets and the terminal (closesupply pass, 2026-09-30)

Every place code fetches bytes and then runs, installs or trusts them, and what
checks it. "Pinned" means a digest or a signature that is in this repository
and is refused on a mismatch; the bump procedure for each is written beside
the pin.

| Fetch | Checked by | State |
| --- | --- | --- |
| bowl: Arch x86_64 bootstrap | `archive.archlinux.org/iso/2026.09.01` (a dated copy), SHA-256 from its `sha256sums.txt`, also checked with gpg against Pierre Schmitz's signature | pinned (was `iso/latest` on TLS alone) |
| bowl: Arch Linux RISC-V | `archriscv-2026-08-27.tar.zst`, SHA-256 | pinned (was `-latest`) |
| bowl: Debian, three arches | the file at one `docker-debian-artifacts` commit per arch, SHA-256 (the branch the old URL followed moves daily; GitHub still serves commits from June that the branch has left) | pinned |
| bowl: Arch Linux ARM | only a `latest` exists, so its build system's RSA-4096 signature (`bowl_signature_ok`: OpenPGP v4, RSA, SHA-256/512, issuer fingerprint and creation time from the hashed area, PKCS#1 through `crypto_rsa_pkcs1`) against a key pinned as numbers and as a fingerprint recomputed on every use, with a floor on the signature date so an older signed archive is refused | signed, pinned key and floor |
| bowl: Alpine, Fedora, Nix | SHA-256 of one release each | pinned (already) |
| bowl: a row with no digest and no key | refused (`bowl_archive_digest_ok` used to take any file) | refused |
| bowl: what the package managers inside a bowl fetch | each distribution's own keyring and index signatures (the keyrings arrive in the pinned archive); pacman's databases are `DatabaseOptional` upstream, `dnf` keeps Fedora's `repo_gpgcheck` default, nix trusts its channel over TLS and its binary cache by key | as each distribution ships it, left; not this tree's to change |
| build: the kernel tarball | a detached PGP signature pinned in `build.c` (the signer's fingerprint is inside it); the tarball that is extracted is the one `gpg --verify` read; the keys are located over WKD, which cannot make the pinned signature verify other bytes | sound, unchanged |
| build: linux-firmware, and the Raspberry Pi firmware | a pinned commit and a pinned digest per file (`kernel/firmware/apply`, `kernel/profile/post/rpi.sh`); a mismatch stops the build | sound, unchanged |
| CI: shellcheck, the actions | the tarball was piped into `tar` with no check and `actions/checkout` was the moving tag `v4`; now a pinned SHA-256 (`sha256sum -c`) and a commit SHA | fixed (`security_hygiene` keeps it) |
| `moonwater update`, `install` | no network path: the image is copied from the medium the session started from | n/a |
| Cloudflare auto-zone header, AIA fetch, NTP | the clock and network streams (closeclock, closenet) | their rows |

A pinned digest ages: Arch's dated bootstrap is about a month old when this
ships, so a first `pacman -Syu` after it may need `archlinux-keyring` first, as
any stale Arch install does, and a dropped mirror copy or a commit GitHub stops
serving is a refused download that means "move the pin".

Secrets on a command line: `moonwater wifi add SSID PASSWORD` and
`moonwater link join NAME SECRET` put the secret in `ps` and in the shell's
history. The history reader now never keeps a line that carries one
(`history_secret_line`), `link join NAME -` reads the secret from standard input
as `wifi add SSID -` reads a password, both warn a person at a terminal, and
`MOONWATER_STRICT` tight refuses the argv forms. A machine script's
`link join NAME SECRET` is the script's own text (the status page says so) and
reads the secret from a file with `-` if that matters.

Boot and a plugged-in disk: boot ran `/root/main.moonwater.sh` as root from
the first attached disk that looked like an install of the same build, and every
copy of a public release says that build. It now chooses in two passes
(`host_census_pick`): the install whose image carries this session's medium
identity (the disk the session started from) first, wherever it enumerates;
failing that the first on a bus nothing is plugged into (`host_disk_external`:
USB or Thunderbolt in the sysfs path, `removable` on the block device, or
`removable` on any device above it, which is how a USB4 enclosure that looks
like any NVMe disk is told). Anything else with the same build is asked about,
with "on a disk that is not this one" in the question; `MOONWATER_STRICT` tight
takes only the first kind. A second disk carrying a copied partition identity
can no longer be resolved in place of the one the census found
(`host_install_refresh` holds the disk). What is not closed: an internal-bus
disk that is not this session's, which the default still takes (D10). Measured:
in a KVM guest an install on a USB disk is found only when its enumeration beats
the census (it did not, at 1.27 s against a 1.0 s floor), so the plug-in case is
racy on the wire and was not reproduced in QEMU; the install lane (117 rows)
still takes the internal NVMe install without asking and asks about another
build. The data partition is mounted without `nodev`/`nosuid` (D09).

The terminal: `edit` wrote a file's bytes and name to the terminal raw (an escape
cleared the screen, a title was set, a bell rang); they are question marks
now (an `edit` lane section with eleven rows, red before). The emulator
(`src/canvas/term.c`) has a coverage-guided target (`term_fuzz`, ASan/UBSan,
720 s clean) and so does bowl's JSON reader (`bowl_json_fuzz`, 620 s clean).
The ranked item "REP with a DECSTBM region costs 70 MB of copying" was read
and not measured: 65,000 cells of REP inside a 134-row region on a 255 x 135
grid takes 119 microseconds.

## Wi-Fi, saved lists and waterlink (closewifi pass, 2026-09-30)

Attackers: a radio in range (spoofed deauthentication, disassociation, EAPOL),
an access point by a saved network's name (an evil twin, with or without
protection or the password), anyone who can read `/root/link.groups`, and a
sender of handshake initiations. Everything below was run on the box against
real peers on `mac80211_hwsim` radios in a guest of the built image: wpa_supplicant's
own access point mode and hostapd 2.11 built from source
(`MOONWATER_HOSTAPD`, the `rekey` family's binary), a third radio made a
monitor that sends unprotected management frames in the access point's name
(`hwsim_radio inject`).

Fixed, each with a test that was red before:

- Management frame protection (66b4e01b). The RSN element carries MFPC and MFPR,
  the join asks the kernel for protection whenever the access point offers it,
  PSK-SHA256 is joined (KDF of 802.11-2016, AES-128-CMAC MIC, descriptor version
  3), the IGTK in message 3 and the group handshake is installed once per key
  from a frame whose MIC checked, and a network whose access point has offered
  protection is written to `/root/wifi.pmf`: an access point of that name that
  does not offer it is never joined, however loud (the twin of a spoofed
  deauthentication). The STRICT tier refuses a secured network that never
  offered it; the default keeps joining those. The `pmf` family (12 rows) is
  red on the PR branch tip (it cannot join an access point that requires
  protection at all) and green: a spoofed deauthentication or disassociation is
  ignored by a protected station and believed by an unprotected one, so the
  frames land. The kernel refuses `NL80211_MFP_OPTIONAL` on a driver with no
  connect of its own ("Operation not supported", found by the first version of
  the test), so protection is asked for as required whenever it is offered.
- TKIP and WPA1 (66b4e01b) stay refused, in words that say why and what to change
  (`wifi add`, bare `wifi`, `status`); a mixed WPA/WPA2 network with a TKIP
  group cipher is refused the same way. This is a decision, not a gap: TKIP is
  broken, and the refusal is now diagnosable.
- WPA3-Personal (c5f9b131, 61cf5e76). SAE over group 19, both the hunt and
  hash-to-element, joined through `NL80211_CMD_AUTHENTICATE` and
  `NL80211_CMD_ASSOCIATE` (a driver of mac80211's kind has no connect of its
  own for SAE), AKM 00-0F-AC:8, PMF required, anti-clogging tokens, a wrong
  password refused in 0.1 s with the words. Interop: wpa_supplicant's access
  point (WPA3 alone, and WPA2+WPA3 with a different PSK so only WPA3 can have
  joined) and hostapd 2.11 with `sae_pwe` 0, 1 and 2 and a token demanded of
  every commit, each held to hostapd's own log (SAE "Accepted", `H2E=` as
  expected, the handshake to its end). Vectors: a reference written from the
  standard in Python (hunt, SSWU, PT, commit, shared secret, KCK, PMK, PMKID,
  both confirms) against the shipped functions in `waterlink_service`. Traps
  found by running it: SAE's key descriptor version is 0, not 3 (hostapd ignored
  version 3 frames), the PTK KDF and MIC must key on the suite and not on the
  version, the EAPOL socket must be open before the association, and a WPA3
  twin whose password is not ours is a candidate now, so the avoid list picks
  the one given up on longest ago. Not built: password identifiers, groups other
  than 19 (and the rejected-groups element), SAE-PK, the extended-key suites
  (AKM 24 and 25).
- Saved lists (5390ba63). `host_write_file` for every persistent state file writes
  `NAME.new`, fsyncs, renames over the old file and syncs the directory, so a
  crash or a signal cannot leave a half or empty `/root/wifi` (RLIMIT_FSIZE 0
  kills the writer part way in the test: the old code left the list empty);
  bluetooth add and remove take the radio lock (twelve at once kept one name).
  `moonwater wifi remove` and `add` wipe the password table on every path
  (checked by reading: no change).
- `/root/link.groups` (8adc87b8). The fast salted SHA-256 beside the PBKDF2 key is
  gone, so a guess costs the 600,000 rounds again for whoever holds the file
  (a script that joins at every boot says `link join NAMESPACE`); an old file's
  check is read as zero and dropped by the next write. The secret is taken from
  standard input (`link join NS -`; the argv form is the shell's closesupply
  work).
- The watcher's own log (65ad5065). The wifi lane's "watch leased the station"
  flake (92 s) was a lost log line: `devkmsg_write` drops what a program writes to
  `/dev/kmsg` past ten lines in five seconds for each open file, without an
  error. The lease was 0.02 to 0.17 s after the association every time it was
  logged. The watcher writes `on` to `kernel.printk_devkmsg`.
- Handshake flood and MSan on waterlink (65ad5065). 11,999 initiations with a good
  mac1 from 900 ports over netem: the cookie path engaged (11,098 cookie
  replies), 0.07 s of listener CPU, +64 kB resident, a paired peer still ran a
  command through it. `waterlink_pre_fuzz` and `waterlink_fuzz` 300 s each under
  MSan, `sh test/run msan` 22 of 22, all clean.
- EAPOL source (e6677642). A frame on the EAPOL socket from any address but the
  access point's is dropped (wpa_supplicant does the same): a message 1 from
  another address used to be answered and its ANonce replaced the pending key
  that message 3 is checked under. An attacker who spoofs both the transmitter
  and the source address of an unprotected frame during the handshake's
  window still can; EAPOL before the keys is unauthenticated by design, and once
  a key is in the kernel drops unprotected EAPOL.

Found sound, and how: the PSK offload path is kept for drivers that have it and
is never used for SAE; a message 3 resent after a group rekey is answered and
installs nothing (the IGTK follows the GTK's rule, and `wifi_eapol_fuzz` now
holds both to "only as the access point made it, never twice", for PSK,
PSK-SHA256 and SAE frames); constant-time field routines carry the SAE work (the
hunt runs every pass whole and selects by mask).

Not testable here: real radios and firmware (hwsim has no firmware, no 6 GHz
regulatory limits and no real rekeys forty minutes in beyond the `rekey`
family), the PSK/SAE offload paths of a driver that implements them.

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
| L01 | netlow #4; `SECURITY.md` | Unauthenticated SNTP is accepted into 2026-09-01..2036-01-01 while the clock is unset; an on-path attacker at boot picks the date certificate checks run under | closed as a narrowing (outcome b): an unset clock takes plain NTP only at or after the floor (2a501362); NTS, which authenticates, sets it when a server is reachable (947a8773); with none reachable the default still takes plain NTP between the floor and 2036, and `moonwater ntp nts only` is the tier that refuses it |
| L02 | nethttp; TLS trust policy | `tls_date_now` takes `time()` with no floor: a rolled-back clock makes an expired certificate current | closed (2a501362): `tls_date_now` refuses a clock below `MOONWATER_CLOCK_FLOOR` and `/root/clock.good` (`TLS_FAULT_CLOCK`, wget status 5, `moonwater time sync` named); `tls_chains` rows with the floor in 2100 and the file at seven values |
| L03 | netlow #4; PR 12 deferral | No NTS (authenticated time) | closed (947a8773): NTS client (RFC 8915): NTS-KE over the TLS 1.3 client (ALPN ntske/1, exporter), AEAD_AES_SIV_CMAC_256 on the existing AES (RFC 5297 vectors and OpenSSL's on three machines), extension fields, cookies spent and replaced, servers in `/root/ntp.nts.servers`; tested against chrony, real servers (Cloudflare, Netnod, PTB, time.nl) and servers that are wrong |
| L04 | TLS trust policy; nettls | Mozilla `distrust-after` and per-anchor constraints not enforced (`anchors.inc` is SPKI-only; Izenpe.com carries 2026-04-15 upstream) | closed (04a90815): per-anchor `distrust-after`, default on; the regenerator reads Mozilla's trust source and reproduces the file byte for byte. Per-anchor NameConstraints: none of the 120 carries any (checked against the box bundle), so nothing to enforce |
| L05 | matrix SNTP row | Era-boundary SNTP integration tests (2036 NTP era rollover, 2038) | closed (f33257c3): `--harness sntp_era`, 108 checks: the 2036 wrap, 2038 and 2104 at the shipped window and at the window moved to the end of era 1 |
| L06 | netlow #2 | DHCP client is a UDP socket: `rp_filter` 1 or 2 drops the OFFER, so `rp_filter` stays 0 in every tier | closed (d9bcf0f2; `rp_filter` 2 by default from e12605d7, 1 in `sec_hardened`) |
| L07 | netlow #3 | No ARP probe or gratuitous ARP for a leased address (`arp_notify`/`arp_announce`/`arp_ignore`/`arp_filter` 0) | closed (fe98aa94; hold-off and release 1322bdcb) |
| L08 | netlow #5 | DNS asks 1.1.1.1 first, in the clear, 16-bit id and kernel source port only, no 0x20 | closed (9b8d47c3, 812e37bf, 054f1f9b) |
| L09 | netlow #6 | General image: IPv6 with SLAAC, RA and redirects accepted though no tool speaks it | closed (e12605d7) |
| L10 | netlow #7 | `fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, unprivileged user namespaces and io_uring on in the default tier | closed (e12605d7; user namespaces left as the kernel has them, D12) |
| L11 | a40bfc9d note | DHCP yiaddr/router sanity (127/8, 224/4, router equal to the address) not refused; rogue-server-only | closed (d54bd70b) |
| L12 | d1bcc3eb note | SYN cookies enabled and read back but not measured under a flood | closed (measured in KVM under a 20,000 pps spoofed flood, this pass) |
| L13 | netlow #1; nettls | No 802.11w/PMF: `wifi_rsn_ie` has no MFPC bit, no IGTK; a spoofed deauthentication frame ends the association | closed (66b4e01b): MFPC/MFPR, USE_MFP required whenever offered, PSK-SHA256, IGTK, `/root/wifi.pmf` (no downgrade), STRICT requires it; `pmf` family against spoofed deauthentication and disassociation |
| L14 | README; netlink2 | No SAE: WPA3-only networks cannot be joined | closed (c5f9b131, 61cf5e76): SAE group 19 by the hunt and by hash-to-element through authenticate/associate; hostapd 2.11 and wpa_supplicant interop; not built: password identifiers, other groups, SAE-PK, AKM 24/25 |
| L15 | netlink2 | TKIP group cipher unsupported (by design) | closed (66b4e01b): still refused, in words that say why and what to set (decision D10) |
| L16 | netlink2 | `/root/wifi` is rewritten in place (a crash mid-write loses the list); bluetooth add/remove take no radio lock (lost update) | closed (5390ba63): written beside itself and renamed, directory synced; bluetooth takes the radio lock |
| L17 | netlink2 | Group `check` in `/root/link.groups` is a fast salted SHA-256: whoever holds the file tests guesses at full speed | closed (8adc87b8): removed; old files read it as zero and drop it on the next write |
| L18 | netlink2 | Live waterlink handshake flood under netem not tested; MSan not run on waterlink | closed (65ad5065): flood 11,999 initiations, cookie path engaged, 0.07 s CPU; MSan 300 s on `waterlink_pre_fuzz` and `waterlink_fuzz`, clean |
| L19 | security-pass-2 #12 | wifi first-message sender unchecked (DoS) | closed (e6677642) as far as it can be: EAPOL from any address but the access point's is dropped; a forger of both transmitter and source before the keys is inherent to unauthenticated EAPOL |
| L20 | security-pass-2 #12 | `moonwater wifi add SSID PASS` puts the password in argv (the `-` stdin form exists) | closed (83d35dd9): the history never keeps the line, a terminal is warned, tight refuses it |
| L21 | secnet #2; bowl.c | Arch/Debian/ArchARM/RISC-V bootstrap tarballs unpinned and unsigned (TLS to the mirror only) | closed (9643436c): dated copies and commit pins by SHA-256, Arch Linux ARM by a pinned RSA key with a date floor; a row with neither is refused |
| L22 | secnet #3 | `link join NS SECRET` puts the group secret in argv | closed (83d35dd9): `link join NS -`, the history, scrubbed argv, tight refuses the argv form |
| L23 | security-pass-2 #1 | Boot takes any attached disk that looks like an install and runs its `main.moonwater.sh` as root (evil-maid); data partition mounted without nodev/nosuid | plug-in media: closed (b5df0b3d), not reproduced in a guest (USB enumerates after the census floor), unit-tested (`boot_links`, `boot_choice`); an internal same-build disk that is not this session's is still taken by default (D10), tight asks; nodev/nosuid is D09 |
| L24 | security-pass-2 #2 | `SPARK_IOCTL_BIND` GET / `MOONWATER_SCRIPT_GET` readable by anyone while settings GET is CAP_SYS_ADMIN | closed (75d016d0, already on this branch: the script text needs CAP_SYS_ADMIN, a bound line reads empty without it; `core_state` rows) |
| L25 | security-pass-2 #3, #6 | `edit` draws file bytes and names raw to the terminal; `term.c` REP with a DECSTBM region costs ~70 MB of copying per printk on a 4K console | closed (d53f353b, a99c369b): `edit` draws controls as `?` and sends no stray byte (fifteen rows, red before); the REP cost was not real, 119 microseconds measured |
| L26 | secnet #4 | bowl JSON has no fuzz target (input is pinned-digest data) | closed (dd3e7319): `bowl_json_fuzz`; every bootstrap is pinned now, so the input is the release's own bytes |
| L27 | PR 16 body; `STRICTER_THAN_WGET` | Default refuses an identical duplicate `Content-Length`, `Content-Length: 3, 3`, obs-fold lines and a control byte in the reason phrase; wget 1.25.0 and curl 8.22.0 accept all four (a conflicting pair stays refused, as curl does) | closed (98507c47): the default takes what wget and curl both take; STRICT_TIGHT refuses all four; generated-heads model at both tiers; a disagreeing Content-Length pair, NUL and bare CR in the status line and a fold after a framing field stay refused in both (curl refuses them) |
| L28 | PR 16 body | Truncated head (FIN inside the status line or a header): wget exits 0 with nothing written, this client fails | closed by decision D11 (truncated heads stay refused, evidence recorded) |
| L29 | PR 16 body; matrix | The tight tier is not fuzzed: `http_fuzz` models the default only; `http_fuzz` was red at 90aefe55 (205 seed, fixed 25eea8af) | closed (98507c47): `http_fuzz_tight` (`-DMOONWATER_STRICT=2`, 409 seeds) found the whole-buffer model disagreeing on a close-delimited 205 with content, fixed; 10 minutes clean at each tier |
| L30 | matrix; secnet #1 | `tls_parse_cert` and its ~75 raw index reads are guarded by hand, fuzzed and judged sound, but not on `byte_reader` | open (closeledger): a code-shape item, not a known defect; the ~75 reads are fuzzed at 20k runs a lane and 20 minutes by hand, all clean |
| L31 | P0 #1; matrix DHCP row | Coverage-guided option-stream fuzzing for DHCP (committed corpus) | closed: `dhcp_fuzz` already was the coverage-guided option-stream target (independent RFC 2131/2132/3396 reference, lease and clock arithmetic); the docs still listed it as a gap. 33 seeds, 10 minutes ASan/UBSan clean; the corpus is its generator (a seed file in the tree fails `security_hygiene` by rule). Forged, tripled, late and NAKed answers in the `netem` lane (624761ce) |
| L32 | matrix TLS row | Record-layer fuzzing for TLS (committed corpus) | closed: `tls_hs_fuzz` already covers the whole record layer and handshake state machine in PRNG-sized reads (95 seeds, 10 minutes ASan/UBSan clean); the docs listed it as a gap |
| L33 | P2 #1; matrix X.509 row | A second independent X.509 path-validator oracle; name-constraints breadth | closed (26403580): Go's `crypto/x509` as a second validator over every `tls_chains` row (1,417 checks after the distrust-after rows), 11 name-constraint rows; nothing found where wget accepts what two validators refuse |
| L34 | P0 #2; matrix DHCP/SNTP rows | Native namespace/netem retransmission and adversarial-scheduling runs are not a mandatory lane | closed (624761ce): `sh test/run netem`, 184 checks, NOT RUN with a reason where namespaces, veth or netem are missing; it is red on the shell before the AF_PACKET commit at rp_filter 1 and 2 (L06) |
| L35 | P0 #2; header | The fuzz budget and the net lane are not mandatory in CI (`ci.yml` is `workflow_dispatch` only) | closed in the file (d9f2ab5c): `.github/workflows/security.yml` runs the net lane and the fuzz lane on push and pull request; it has not run on a hosted runner yet (PR 16 conflicts with main, and GitHub runs `pull_request` workflows only for a PR it can merge), so the manual command list stays in the file's header |
| L36 | P0 #3 | MSan (`sh test/run msan`) is exercised on aarch64 Linux clang only and on hosted lifts, not a freestanding `CHECK_net` | open (closeledger) |
| L37 | P1 #1, #2, #4 | No explicit CPU-work budget per parser loop; clock-jump faults and SNTP fault injection; long pauses, simultaneous timeout and the TLS and redirect legs of hostile scheduling | open (closeledger) |
| L38 | P1 #3 | ARM64 and RISC-V network lanes are not required anywhere; sanitizer-capable builds there are not run | in progress (closeledger): the CI job requires the net-x86_64, net-arm64 and net-riscv64 tallies, and `sh test/run net` runs all three under qemu-user; sanitizer builds exist on x86_64 only |
| L39 | P2 #3, #4 | Static-analysis reports with suppressions reviewed; an independent review after the P0 lanes are reproducible | open (closeledger; user decision for the review) |
| L40 | kit lane | `sh test/run kit` is 39 of 41 on this branch and on main (firmware line pins, performance map BENCH sections); not a network defect, carried for honesty | open (main's, not this branch's) |
| L41 | found by the `netem` lane | The DHCP client waits for the ACK of its REQUEST only a quarter second (and an OFFER 250 ms for the first twelve attempts): against a server that answers 1.5 s late under 25% loss the lease took 28 s and 16 DISCOVERs (availability on a high-latency link, not an attack) | closed (not reproduced: the `netem` rows give 3.0 s on the parent and 3.1-3.4 s here for a server 1.5 s away, with and without loss, because the attempts' waits lengthen after twelve and 1322bdcb bounds the ACK wait to two seconds; `net netem` now asserts under 15 s) |
| L42 | found by the `netem` lane | SNTP shares one ten second deadline across its five samples: a datagram lost first spends all of it and the rest are sent with no time to wait, so one lost packet in four ends a query with no answer (pinned as the KNOWN OPEN `sntp drop2` row; availability, not an attack) | closed (730f243d): each SNTP sample has its own two-second wait; the `netem` lane's `sntp drop2` row moved from KNOWN OPEN to an answer taken |
| L43 | netlow | A DHCP exchange cut between REQUEST and ACK leaves the server holding the address | closed (1322bdcb) |

### Decisions (closed by decision, reasons recorded; the user can override)

| Id | Decision | Reason |
| --- | --- | --- |
| D01 | No OCSP, CRL, CRLite or Certificate Transparency | Same as default wget and curl; a revoked certificate stays valid until it expires |
| D02 | No CN fallback | GnuTLS-based wget has one, Chrome none; refusing is deliberate |
| D03 | DNSSEC is out of scope | The resolver is a stub that trusts the network's DNS; validating would need a trust-anchor and time story this image does not have while L01-L03 stand. Closeclock has landed (L01-L03); revisit |
| D04 | URL refusals kept: userinfo, `%00`/`%0d`/`%0a` in a target, a backslash in an authority, a URL of 2,048 bytes or more, an IDN host (no IDNA), HTTPS-to-HTTP redirect, head over 16 KiB | Named and pinned in `wget_hostile` and `http_urls`; stack buffers bound them |
| D05 | Saved file name is the escaped, not the decoded, name | Decoding needs `%2f`/`%00`/`%1b` re-escaping; safer as is |
| D06 | A body dripped one byte per 29 s runs forever; no Content-Length pre-check for disk fill | Each read has its own 30 s timeout as GNU wget; GNU has no pre-check either |
| D07 | HEAD not exercised | The client only builds GET |
| D08 | "Guest is root, not a wall": root is not hardened against itself | Documented policy in `SECURITY.md` |
| D09 | The data partition is mounted without `nodev` and `nosuid` | It holds the user's `/root`, `/home` and the bowls; a bowl's `sudo` and `su` need setuid, and a device node on it can only have been made by root. Whose disk boot takes without asking is the control (L23), and the user can override |
| D10 | TKIP and WPA1 networks stay refused | TKIP is broken; the refusal now names what to change at the access point (WPA2 with AES). The user can override |
| D10 | The default still takes, without asking, the first same-build install on an internal bus when none is the disk the session started from | Reaching it means opening the case and adding a disk, which already gives write access to the unencrypted disk and the machine; asking about every internal disk would put a question on every boot from a live stick. `MOONWATER_STRICT` tight asks, and the user can make that the default |
| D11 | A response cut inside its status line or a header stays a failure in both tiers | Measured against GNU wget 1.25.0 and curl 8.22.0 from a raw-socket server: cut at byte 8, wget exits 4 and curl 52; at 15, wget exits 0 with nothing written and curl 52; at 24 (inside a header), both exit 0 with nothing written; right after the last header line, wget exits 4 and curl 18. The two references disagree with each other, so no tier is 1:1 with both, and accepting a cut as success turns a download an attacker (or a dropped connection) truncated into an empty success that a script testing the exit status cannot tell from a real empty file. The user can override by changing `STRICTER_THAN_WGET`'s `FIN cuts` rows in `wget_mutation` and the no-reply status |
| D12 | AEAD_AES_128_GCM_SIV is not offered in NTS-KE | RFC 8915 makes AEAD_AES_SIV_CMAC_256 the mandatory algorithm and every public NTS server (Cloudflare, Netnod, PTB, time.nl, chrony) supports it; GCM-SIV needs POLYVAL and its key derivation, about 150 lines for no server that needs it. Offer it if one ever does |
| D13 | Plain NTP stays the fallback when no NTS server can be reached, marked plain and never written to the floor | The default is 1:1 with what a machine without NTS did; `moonwater ntp nts only` is the tier that refuses it. A network that blocks TCP 4460 costs one try of a few seconds and two minutes without trying NTS again |
| D12 | Unprivileged user namespaces are left as the kernel has them | Root is unaffected, no tool here creates one, and `user.max_user_namespaces` 0 would also take them from the test harnesses that run on the same kernel; a kiosk tier that wants them off sets it itself. `unprivileged_bpf_disabled` has no knob: the image is built without `bpf(2)` (classic socket filters, which the DHCP packet socket uses, are separate) |
| D13 | DNS over TLS is off by default; `tls` (with UDP fallback) and `tls-only` are opt-in | A captive portal answers or drops port 853 and a first lease comes with the clock unset, where a certificate that has not begun is refused: both are an ordinary first minute. `tls` survives them at up to two seconds a lookup, which a default should not cost; `tls-only` cannot have its clock set by names it cannot resolve, so the time servers' names (and hostid, logger's server, waterlink's peers) are always asked in plain. Only wget and host follow the setting. The user can change the default |
| D14 | The default conflict probe listens for about 0.2 s, not RFC 5227's 4-7 s | Four to seven seconds would more than double the time to a lease; a kernel answers ARP in a millisecond or two on a wired LAN. A host that answers slower (a radio in power save) can be missed; `sec_hardened` keeps RFC 5227's numbers. The user can change the default |
| D15 | `rp_filter` is 2 (loose) by default and 1 in `sec_hardened` | 2 is what Debian ships and is lease-safe; 1 drops asymmetric traffic by design and was measured on one leased link only |
