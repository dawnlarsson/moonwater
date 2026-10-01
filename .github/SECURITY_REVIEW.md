# Network security review status

This is a gap register, not a claim that the network stack is “bulletproof.”
It exists to stop security work becoming a loop of adding another isolated
malformed packet. A pass counts as new evidence only when it closes a named
threat family, demonstrates that the test fails without the fix, or removes a
gap below.

## Current assessment

The stack has strong hand-written boundary checks and unusually broad
deterministic regression coverage. Local lanes exist for coverage-guided
fuzzing under ASan/UBSan (`sh test/run fuzz`: TLS, DER, DHCP, SNTP, DNS,
netlink, Wi-Fi, crypto, waterlink and the HTTP response section), MSan over
hosted parser lifts (`sh test/run msan`), resource-exhaustion and mid-path
fault sweeps, `http.client`, curl and GNU wget as oracles for HTTP framing and
delivery, and a `netem` lane that runs the DHCP and SNTP clients against an
adversarial server in namespaces. It is **not yet release-grade evidence for
memory safety or parser completeness** because those lanes are not mandatory
(the CI `security` job is parked, `workflow_dispatch` only), the HTTP fuzz
target models the default tier only, and a corpus beyond the generated seeds is
not kept in the tree (seed files are banned by `security_hygiene`).

| Area | Evidence present | Important remaining gap |
| --- | --- | --- |
| Netlink | sender PID, sequence, length/alignment, multipart and truncation checks; `netlink_fuzz` | persistent fuzzing of nested attributes beyond the bounded target |
| DNS | exact question/ID binding, compression loops, full RR framing, UDP truncation to TCP; the source port drawn per query, 0x20 case mixing and EDNS0 with a fallback for each; the network's own resolver asked first and its first answer final; `dns_fuzz` | independent packet oracle; DNSSEC is out of scope (D03) |
| TLS records/handshake | record and handshake fragmentation, transcript/Finished, AEAD limits, state ordering, `tls_hs_fuzz` libFuzzer target over the record layer and handshake state machine | a corpus beyond the generated seeds |
| X.509 | strict DER and generated-chain policy matrix against OpenSSL and, as a second independent path validator, Go's `crypto/x509` (`tls_chains`); `tls_der_fuzz` / `tls_verify_fuzz` libFuzzer targets; DNS-name and IP matching against OpenSSL's own check (`tls_hostnames`); validity times against RFC 5280 (`tls_dates`); Wycheproof's vectors under the production crypto (`crypto_vectors --wycheproof`); Mozilla's server-auth `distrust-after` dates per anchor | name constraints in more shapes than the matrix has |
| HTTP/URL | sink-side request validation, framing conflicts, 204/205/304 as wget and curl take them with the RFC 9110 framing at the tight tier, split-point and chunk/trailer checks, `http.client` and curl framing oracles, HTTPS downgrade harness, GNU wget and curl as live oracles for byte-at-a-time, split, FIN/RST and 1xx-storm delivery (`wget_mutation`), hostile servers and redirect shapes against the built wget (`wget_hostile`), a redirect from public to non-public address space refused at the tight tier | the tight tier is not fuzzed (`http_fuzz` models the default); TLS and redirect legs of hostile scheduling |
| DHCPv4 | peer/xid/MAC binding, option framing/overload, state cross-product, entropy faults, leases a server has no business handing out refused, the watcher's exchange cut once by link news rather than by every carrier flap; the `netem` lane (clean, tripled, forged, late, dropped and NAKed answers, plain and under loss, duplication and reordering); `dhcp_fuzz` (coverage-guided option stream) | DHCP over a raw packet socket and address-conflict probing are separate changes |
| SNTP and NTS | nonce and peer binding, ancillary timestamp parsing, arithmetic and selection checks; NTS (RFC 8915): the AES-SIV-CMAC-256 AEAD against OpenSSL's and RFC 5297's vectors on three machines, the NTS-KE reply parser and the NTP extension fields under `nts_fuzz`, every reply byte changed, cut or replayed refused against independently built replies, and the client against chrony's NTS server and against key-establishment and time servers that are wrong (`nts`); the clock's floor and `/root/clock.good` in `tls_chains` and `moonwater_cli` | the `netem` lane (forged, tripled, late, dropped answers, two lost requests still answered) and `sntp_era` (the 2036 wrap, 2038 and 2104 at the shipped window and at the window moved to the end of era 1); `nts` needs a chronyd built with NTS (`MOONWATER_CHRONYD`) and is NOT RUN without one | plain NTP remains the fallback and is unauthenticated (`moonwater ntp nts only` refuses it); AEAD_AES_128_GCM_SIV is not offered (D12) |
| Wi-Fi | RSN and EAPOL-Key handling, replay counters, scan-result parsing (`wifi_scan_fuzz`, `wifi_eapol_fuzz`), an access point's name read from its first name element only, a join that prefers the access points that offer what the saved network asks for, EAPOL frames accepted only from the access point | management-frame protection and WPA3 are separate changes |
| Waterlink and saved state | Noise handshake, cookie, replay window, grants; the saved wifi and bluetooth lists written beside themselves and renamed; no hash of the group secret in `/root/link.groups` | handshake flood under netem; MSan on waterlink |

The unauthenticated-protocol limits in `SECURITY.md` remain fundamental: an
on-path attacker can forge DHCPv4, ordinary DNS, and SNTP. Parser hardening
does not convert those protocols into authenticated ones.

### TLS trust policy (decided, not gaps)

- **Revocation and transparency: none.** No OCSP, CRL or CRLite, and no
  Certificate Transparency check, as with the default `wget` and `curl`; the
  client verifies a chain to a Mozilla root and nothing more. A revoked
  certificate stays valid until it expires.
- **Names: SAN only.** A leaf without a matching subjectAltName is refused; the
  common-name fallback that GnuTLS-based `wget` still has is deliberately not
  implemented (Chrome has none). A wildcard's star is one whole label, never on
  a public suffix, and may stand for an underscore label.
- **Anchors are keys.** `anchors.inc` holds 120 Mozilla server roots as keys,
  regenerated and cross-checked with `--harness anchors --bundle`. Mozilla's
  server-auth `distrust-after` dates are enforced: each row carries the date
  (Izenpe.com, 2026-04-15 23:59:59) and a chain whose leaf's notBefore is later
  than the date of the root it ends at is refused, by default, on both ways a
  chain ends (as NSS, Firefox and Chrome do; openssl has no such concept, so
  `tls_chains` names the refusals `DELIBERATE`). The file is regenerated by
  `differential.py --harness anchors --bundle B --trust-source T --write`,
  which records both input digests in the header and must reproduce the file
  byte for byte. Anchor validity dates are not enforced; none of the 120
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

## Priority queue

### P0 — evidence needed before a high-assurance claim

1. Persistent fuzzing: done for TLS (DER, certificate lists, handshake and
   record layer), DNS names and RRs, HTTP response framing and chunks (the
   default tier), DHCP option streams, SNTP replies, netlink, Wi-Fi, crypto and
   waterlink, seeded from generators and run under ASan+UBSan. Open: the HTTP
   target at the tight tier, and a corpus beyond the generated seeds.
2. Make x86-64 ASan+UBSan and native namespace/netem runs required CI jobs.
   `MOONWATER_FUZZ_REPORT=… sh test/run fuzz` already records compiler and sanitizer
   versions, seed counts, budgets and exits; it has to run on every release,
   not by hand. The native namespace runs are the `netem` lane
   (`sh test/run netem`, NOT RUN where a host lacks `--map-auto`, veth or
   netem). The CI `security` job stays parked.
3. Keep MSan (`sh test/run msan`) separate from UBSan and widen it from hosted
   lifts toward a full freestanding `CHECK_net`; today it is exercised on
   aarch64 Linux clang only.
4. Response framing has `http.client` and curl beside the written matrix, and
   `wget_mutation` runs GNU wget and curl over the same delivery schedules;
   keep pinning Moonwater's policy for every disagreement (`DELIBERATE`,
   `TIGHT_REFUSES`, `STRICTER_THAN_WGET`).

### P1 — resource and state-machine assurance

1. Byte, item and recursion ceilings are recorded and hit-tested
   (`SECURITY_TEST_MATRIX.md`); add an explicit CPU-work budget per parser or
   transaction loop.
2. Allocation, `mmap`, descriptor, short-I/O, `EINTR` and deadline faults are
   injected for HTTP, TLS, DNS, DHCP and netlink; add clock-jump faults and
   extend the rest to SNTP.
3. Make ARM64 and RISC-V network lanes required, including sanitizer-capable
   hosted builds where available. “Not run” is not evidence.
4. Deterministic hostile scheduling: done for HTTP responses by
   `wget_mutation` (one-byte delivery, every grammar boundary, early FIN/RST,
   a 1xx storm); open for TLS and for the redirect legs.

### P2 — independent and operational assurance

1. A second X.509 path validator now runs beside OpenSSL over every
   `tls_chains` row (Go's `crypto/x509`); a durable corpus of real and
   synthetic certificates, including more name-constraint shapes, is open
   (`x509_corpus` runs by hand against 643 public hosts).
2. Namespace tests with loss, duplication, reordering and delay run for DHCP
   and SNTP (`netem` lane); MTU changes, stale queued datagrams and
   source-address changes at every protocol phase are open.
3. Add static-analysis and compiler-hardening reports to releases, with every
   suppression reviewed. Static analysis is supporting evidence, not a proof.
4. Commission an independent review after the P0 lanes are reproducible; an
   internal checklist cannot establish its own completeness.

## Open issue ledger

What this change closed, and what it leaves open. Features that were
proposed to close a documented gap are separate branches and pull requests,
not part of this one; they are listed with the gap they would close.

### Closed here

| Area | Issue | Evidence |
| --- | --- | --- |
| HTTP | 204 and 205 framing: the response boundary a client and an intermediary read must agree | the framing matrix at both tiers (`http_response_framing`, `CHECK_net` at `MOONWATER_STRICT` 2), `wget_mutation` against GNU wget and curl; the default does what wget and curl do, the tight tier holds RFC 9110 |
| HTTP | a redirect from public address space into this host or its network (the SSRF and DNS-rebinding half) | tight tier refuses it, `wget_hostile` address-policy rows |
| DHCP | a lease a server has no business handing out (0/8, 127/8, 224/3, a router equal to the address) | the lease-sanity grid in `CHECK_net` |
| DHCP | a carrier flap on any link cut the watcher's exchange each time | `net_news_may_cut`, the four-second hold-off after a cut |
| DNS | a 16-bit id and a kernel-chosen source port were all that bound a reply; the public resolver was asked before the network's own | `resolving_policy` and the namespace walk check (`net_test_dns_walk`) in `CHECK_net` |
| TLS | Mozilla `distrust-after` was not enforced | `tls_chains` rows, `--harness anchors` regenerator |
| Kernel | no SYN-flood defence in the image; ICMP redirects, source routes, IPv6 router advertisements and TIME-WAIT assassination on by default | `CONFIG_SYN_COOKIES`, `net_kernel_defaults`, the boot-lane row, `sec_hardened` |
| Wi-Fi | an access point's name read from the first non-empty name element, not the first (a differential with the kernel) | `wifi_scan_fuzz` seeds, `wifi_air` |
| Wi-Fi | a join by name sat out its timeout on a louder open or WPA3-only twin of the saved network | the `twin` family of `wifi_air` |
| Wi-Fi | EAPOL frames from any address were answered | `wifi_source_checks` |
| Saved state | `/root/wifi` and `/root/bluetooth` rewritten in place; bluetooth add and remove without the radio lock | `moonwater_cli`: a write cut short leaves the saved list as it was |
| Waterlink | `/root/link.groups` held a fast salted SHA-256 of the secret, a guessing oracle for anyone who could read it | `link` lane |
| SNTP | five samples shared one ten-second deadline, so one lost datagram in four ended a query | the `netem` lane's SNTP scenes |
| Supply chain | bowl's Arch, RISC-V Arch and Debian bootstraps rested on TLS and a mirror alone | pinned digests, a pinned signing key for Arch Linux ARM, `bowl` lane |
| Time | `tls_date_now` took `time()` as it was, so a clock rolled back or never set made an expired certificate current, or every certificate not yet valid | the clock floor: `tls_chains` (a floor in 2100 refuses with status 5; `clock.good` a day ahead, a day behind, text, past 2036, before the build, trailing text, absent), `moonwater_cli` (`time sync` with ten kinds of `clock.good`; shutdown keeps the file forward only and never writes through a link) |
| wget | a certificate it cannot verify made wget exit 4 with "TLS handshake failed" whatever the reason; GNU wget exits 5 and says which way the dates fell, whose names do not match, or that the issuer is unknown | every refused chain of `tls_chains` exits 5 with GNU's wording, "has expired" and "is not yet activated" for the dates |
| Time | Plain SNTP moves the clock anywhere between the floor and 2036, so an on-path attacker at boot picks the date certificate checks run under | NTS is asked first and sets an unset or wrong clock when a server is reachable; with none reachable the default still takes plain NTP between the floor and 2036, and `moonwater ntp nts only` refuses it: a narrowing, not a closure |
| Time | No authenticated time | an NTS client (RFC 8915): NTS-KE over the TLS 1.3 client, AES-SIV-CMAC-256 on the existing AES (RFC 5297's vectors and OpenSSL's on three machines), the `nts` harness against chrony 4.8's NTS server in a namespace (88 checks), `nts_fuzz`; Cloudflare, Netnod, PTB and time.nl answer with their own key establishment, and in KVM guests with the RTC at 2000-01-01, 2026-11-15 and 2028-10-01 NTS set the time 4.5 s into the boot |

### Open, in separate branches

| Gap | Branch |
| --- | --- |
| 802.11w management-frame protection, WPA3-SAE | `feature/wpa3-pmf-sae` |
| DNS over TLS | `feature/dns-over-tls` |
| DHCP over a raw packet socket (so `rp_filter` can be on), ARP address-conflict probing, the exchange not cut between REQUEST and ACK | `feature/dhcp-packet-socket-acd` |
| The default accepts what wget and curl accept in a header block, URL spelling and redirect statuses | `feature/wget-curl-parity` |
| `edit` sends file bytes to the terminal raw; a secret on a command line is kept by the shell history; boot takes the first install that looks like this build; `fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, `io_uring_disabled` defaults | `hardening/outside-network` |

### Open, with no change planned here

- Plain SNTP is accepted into any moment the build and clock window allow;
  only authenticated time narrows that.
- The DHCP client reads its OFFER from a UDP socket, so `rp_filter` 1 or 2
  drops it; the image keeps `rp_filter` at the kernel's value.
- A body dripped one byte per 29 seconds runs forever (each read has its own 30
  second timeout, as GNU wget's and curl's without `--max-time`), and the
  endless-body disk fill has no Content-Length pre-check (GNU has none).
- HEAD is not exercised: the client builds only GET.
- A response cut inside its status line or a header stays a failure in both
  tiers. GNU wget exits 0 with nothing written at some cut points and curl
  fails at others, so no tier split matches both, and a cut accepted as
  success would be an empty success.
- `tls_parse_cert` and its raw index reads are guarded by hand, fuzzed and
  judged sound, but are not on `byte_reader`.
- The CI `security` job is parked by design and this change does not touch it.

### Decisions

| Id | Decision | Reason |
| --- | --- | --- |
| D01 | No OCSP, CRL, CRLite or Certificate Transparency | Same as default wget and curl; a revoked certificate stays valid until it expires |
| D02 | No common-name fallback | GnuTLS-based wget has one, Chrome none; refusing is deliberate |
| D03 | DNSSEC is out of scope | The resolver is a stub that trusts the network's DNS |
| D04 | Defaults hold what GNU wget 1.25.0 and curl 8.22.0 do for 204 and 205; the RFC 9110 framing is the tight tier's | Real servers send a 204 with `Content-Length: 0`, and the client closes the connection after the one response, so an unread declared body can never be taken for the next response |
| D05 | "Guest is root, not a wall": root is not hardened against itself | Documented policy in `SECURITY.md` |
| NTS-D1 | AEAD_AES_128_GCM_SIV is not offered in NTS-KE | RFC 8915 makes AEAD_AES_SIV_CMAC_256 the mandatory algorithm and every public NTS server (Cloudflare, Netnod, PTB, time.nl, chrony) supports it; GCM-SIV needs POLYVAL and its key derivation, about 150 lines for no server that needs it. Offer it if one ever does |
| NTS-D2 | Plain NTP stays the fallback when no NTS server can be reached, marked plain and never written to the floor | The default is 1:1 with what a machine without NTS did; `moonwater ntp nts only` is the tier that refuses it. A network that blocks TCP 4460 costs one try of a few seconds and two minutes without trying NTS again |

SYN flood recipe (by hand): in a network namespace with two taps, a guest on
one (a scripted DHCP server serves it, and an HTTP server serves a small static
listener that accepts and closes), `wget` the listener into the guest, start it,
then send random-source SYNs at it (a raw `IP_HDRINCL` sender, 20,000 packets a
second for 14 s) while a second address of the namespace connects every quarter
second and counts the answers. Toggle `/proc/sys/net/ipv4/tcp_syncookies` in the
guest between runs. With cookies on the kernel logged "Sending cookies" and 48 of
48 connections from a host it had not met were answered; with them off, 0 of 6.
A peer the kernel has already seen succeed gets through with cookies off too (the
last quarter of the SYN backlog is kept for destinations it has proven), so the
test host must be one that has not.

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
