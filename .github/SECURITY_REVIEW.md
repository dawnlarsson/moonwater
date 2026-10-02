# Network security review status

This is a gap register, not a claim that the network stack is “bulletproof.”
It exists to stop security work becoming a loop of adding another isolated
malformed packet. A pass counts as new evidence only when it closes a named
threat family, demonstrates that the test fails without the fix, or removes a
gap below.

The composition and hostile-scheduling campaign is scoped in
[`NETWORK_ATTACK_PLAN.md`](NETWORK_ATTACK_PLAN.md). The follow-on adversarial
campaign in [`NETWORK_ATTACK_PLAN_IV.md`](NETWORK_ATTACK_PLAN_IV.md) attacks
boundaries that campaign intentionally did not own: canonicalization between
protocols, ambient authority, entropy continuity, feedback loops, side
channels, and kernel/offload semantic disagreement. Neither plan is evidence
that its hypotheses have been closed.

The shared primitives under `net.c`, including `lib.c`'s production crypto,
are inventoried with their evidence in
[`NETWORK_DEPENDENCY_CLOSURE.md`](NETWORK_DEPENDENCY_CLOSURE.md). Its automated
gate fails when `net.c` begins calling a new primitive without a reviewed test
assignment; skipped lanes still prevent a release claim of a closed loop.

Finite arithmetic claims that can be proved rather than sampled are recorded
in [`NETWORK_PROOFS.md`](NETWORK_PROOFS.md) and checked by `net_math_proof` at
the start of the net lane. The proof boundary is intentionally narrower than a
claim that the entire protocol stack is formally verified.

## Current assessment

The stack has strong hand-written boundary checks and unusually broad
deterministic regression coverage. Local lanes exist for coverage-guided
fuzzing under ASan/UBSan (`sh test/run fuzz`: TLS, DER, DHCP, SNTP, DNS,
netlink, Wi-Fi, crypto, waterlink, bowl's OpenPGP signature reader and the HTTP
response section), MSan over
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
| HTTP/URL | sink-side request validation, framing conflicts, 204/205/304 as wget and curl take them with the RFC 9110 framing at the tight tier, split-point and chunk/trailer checks, `http.client` and curl framing oracles, HTTPS downgrade harness, GNU wget and curl as live oracles for byte-at-a-time, split, FIN/RST and 1xx-storm delivery (`wget_mutation`), hostile servers and redirect shapes against the built wget (`wget_hostile`), a redirect from public to non-public address space refused at the tight tier; `http_fuzz` binds each hop's parsed/resolved host to its connect tuple, TLS hostname and serialized Host field | CNAME and multiple-address fallback identity; interface/scope identity; tight-tier TLS redirect legs under hostile scheduling |
| DHCPv4 | peer/xid/MAC binding, option framing/overload, state cross-product, entropy faults, leases a server has no business handing out refused, ARP address-conflict probing before installation, the watcher's exchange cut once by link news rather than by every carrier flap; the `netem` lane (clean, tripled, forged, late, dropped and NAKed answers, plain and under loss, duplication and reordering); `dhcp_fuzz` (coverage-guided option stream) | DHCP over a raw packet socket is a separate change |
| SNTP | nonce and peer binding, ancillary timestamp parsing, arithmetic and selection checks, each sample with its own wait, era-boundary integration (`sntp_era`: 2036, 2038, 2104), the `netem` lane | plain SNTP is unauthenticated; authenticated time is a separate change |
| Wi-Fi | RSN and EAPOL-Key handling, replay counters, scan-result parsing (`wifi_scan_fuzz`, `wifi_eapol_fuzz`), an access point's name read from its first name element only, a join that prefers the access points that offer what the saved network asks for, EAPOL frames accepted only from the access point | management-frame protection and WPA3 are separate changes |
| Waterlink and saved state | Noise handshake, cookie, replay window, grants; the saved wifi and bluetooth lists written beside themselves and renamed; no hash of the group secret in `/root/link.groups`; hosted mDNS sanitizer/model generates valid PTR+SRV intersections in both orders, repeats and capacity overflow | handshake flood under netem; MSan on waterlink |

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
- **Not in ring 0.** No `crypto_`, `tls_` or X.509 code is in the include graph
  of `src/moonwater/core.c`; `net.c` is compiled into userspace programs only.

## Priority queue

### P0 — evidence needed before a high-assurance claim

1. Persistent fuzzing: done for TLS (DER, certificate lists, handshake and
   record layer), DNS names and RRs, HTTP response framing and chunks (the
   default and the tight tier), DHCP option streams, SNTP replies, netlink, Wi-Fi, crypto,
   waterlink and bowl's OpenPGP signature reader, seeded from generators and run under ASan+UBSan. Open: a
   corpus beyond the generated seeds.
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
   (`SECURITY_TEST_MATRIX.md`). DNS, DHCP, SNTP and netlink now also cap
   hostile or unrelated datagrams per receive phase. HTTP and TLS parser work
   is charged to their byte, header, handshake and record ceilings; continue
   auditing any newly introduced transaction loop before it enters the
   dependency manifest.
2. Allocation, `mmap`, descriptor, short-I/O, `EINTR` and deadline faults are
   injected for HTTP, TLS, DNS, DHCP and netlink. The shared absolute-deadline
   arithmetic now runs over zero, backward and forward clock jumps; extend the
   remaining allocation and I/O faults to SNTP itself.
3. The release command now makes ARM64 and RISC-V network lanes required and
   enables freestanding UBSan traps (`MOONWATER_REQUIRE_ARCHES=1 MW_UBSAN=1 sh
   test/run net`); hosted ASan/UBSan and MSan remain required companion reports.
   Ordinary developer lanes may still soft-skip. “Not run” is not release
   evidence.
4. Deterministic hostile scheduling: HTTP responses use `wget_mutation`
   (one-byte delivery, every grammar boundary, early FIN/RST, a 1xx storm),
   TLS handshake/record reads use the seeded `tls_hs_fuzz` chunk scheduler,
   and every live HTTPS redirect leg is repeated whole, byte-at-a-time and at
   HTTP grammar boundaries by `https_downgrade`.
5. Every HTTP tier gives the complete response body a five-minute absolute
   budget in addition to the per-read idle timer. The socket regression drips
   valid payload bytes faster than the idle timer and proves that progress
   cannot renew the transaction deadline. Syntax and framing retain their
   documented tier differences; resource lifetime does not.

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
| HTTP | a redirect from public address space into this host or its network (the SSRF and DNS-rebinding half) | tight tier refuses it, `wget_hostile` address-policy rows, `http_fuzz` at the tight tier (no connection inside after one to public space) |
| HTTP/URL | `https:443` was reinterpreted as plaintext to a host named `https`, although an RFC URI parser assigns the same bytes the HTTPS scheme | `CHECK_net` URL identity rows reserve bare `http:`/`https:` in every ASCII case while retaining schemeless `h:81` |
| HTTP/URL | non-web URI spellings such as `ftp:21` and `smtp:25` were still reinterpreted as plaintext HTTP host:port pairs | the tight tier treats every leading RFC 3986 scheme token as a scheme and requires explicit `http://` or `https://`; default shorthand compatibility remains named and tested |
| Waterlink mDNS | a failed multicast answer advanced `last_answer`, and unsigned age wrapped on a backward clock sample | `CHECK_waterlink_service` injects an invalid socket and a timestamp older than the saved answer; neither consumes nor bypasses the shared interval |
| Waterlink groups | legacy fast-verifier migration ignored short/error writes and durability failure | positional all-write plus required `fsync`; migration failure activates no groups and `/dev/full` proves the refusal path |
| Waterlink mDNS | failed `IP_MULTICAST_IF` selection fell through to `sendto` using the socket's stale interface | real socket primes loopback, then an impossible ifindex must refuse before send; selection and advertised address remain one transaction |
| Waterlink reliability | future flight/ack timestamps underflowed elapsed time and triggered immediate loss or acknowledgement | `CHECK_waterlink` puts both timestamps ahead of `now`; loss, timeout and delayed-ack transitions remain closed |
| SNTP | a UDP datagram longer than the 48-byte message this client parses was silently truncated to 48 bytes and accepted by its prefix | the receive buffer carries one sentinel byte and the exchange requires an exact 48-byte datagram; `machine_sntp` sends exact and one-byte-over datagrams through the production `recvmsg` helper |
| DHCP | a server could lease an address already active on the link and the client installed it without conflict detection | three ARP probes precede installation; another station's request, reply, or simultaneous zero-source probe for the offered address refuses the lease, and inability to probe fails closed |
| DHCP | a lease a server has no business handing out (0/8, 127/8, 224/3, a router equal to the address) | the lease-sanity grid in `CHECK_net` |
| DHCP | a carrier flap on any link cut the watcher's exchange each time | `net_news_may_cut`, the four-second hold-off after a cut |
| DNS | a 16-bit id and a kernel-chosen source port were all that bound a reply; the public resolver was asked before the network's own | `resolving_policy` and the namespace walk check (`net_test_dns_walk`) in `CHECK_net` |
| TLS | Mozilla `distrust-after` was not enforced | `tls_chains` rows, `--harness anchors` regenerator |
| Kernel | no SYN-flood defence in the image; ICMP redirects, source routes, IPv6 router advertisements and TIME-WAIT assassination on by default | `CONFIG_SYN_COOKIES`, `net_kernel_defaults`, the boot-lane row, `net_sysctl` (every interface, however many), `sec_hardened` |
| Wi-Fi | an access point's name read from the first non-empty name element, not the first (a differential with the kernel) | `wifi_scan_fuzz` seeds, `wifi_air` |
| Wi-Fi | a join by name sat out its timeout on a louder open or WPA3-only twin of the saved network | the `twin` family of `wifi_air` |
| Wi-Fi | EAPOL frames from any address were answered | `wifi_source_checks` |
| Wi-Fi | the first 64 distinct SSIDs in kernel dump order filled the bounded air list, so weak forged names could hide a stronger real network | `wifi_air_capacity`: a full weak-name set admits a stronger network, refuses a weaker 65th, and never evicts the associated row |
| Wi-Fi | aggregation by SSID combined the `joined` bit of the associated BSSID with a louder twin's security, frequency and signal, presenting two radio identities as one trusted row | `wifi_air_capacity` puts an OPEN loud twin before and after a weak joined WPA2 BSSID and requires every retained field, including BSSID, to remain the joined one's |
| Saved state | `/root/wifi` and `/root/bluetooth` rewritten in place; bluetooth add and remove without the radio lock | `moonwater_cli`: a write cut short leaves the saved list as it was |
| Waterlink | `/root/link.groups` held a fast salted SHA-256 of the secret, a guessing oracle for anyone who could read it | `link` lane |
| Waterlink mDNS | a forged legacy-unicast question with source port zero spent the global reply interval even though the kernel refused the response, starving honest one-shot askers without producing attack traffic | `waterlink_service` puts port zero immediately before an honest asker; zero is neither answered nor treated as multicast, and only a complete successful send advances `last_reply` |
| Waterlink mDNS | PTR/ANY questions and SRV records were acted on without requiring DNS Internet class, so wrong-class packets that other responders ignore could elicit replies or greeting work | `CHECK_waterlink` refuses a CHAOS question as an ask, retains Internet-class QU, and `waterlink_fuzz` carries the parser under ASan/UBSan |
| Waterlink mDNS | a query with nonzero RCODE reached the responder, and a zero-TTL SRV goodbye was rediscovered as a live endpoint and launched greeting work | `CHECK_waterlink` sends the malformed query and an announcement built by the production writer with TTL zero; neither produces an ask/live instance |
| Waterlink mDNS | SRV ports were consumed without parsing the target name or requiring exact RDATA, so port zero, the unavailable root target and malformed trailing targets could launch greetings | `CHECK_waterlink` mutates the production announcement into root/trailing and port-zero records; `waterlink_service` proves a packet family of port-zero records spends no entropy or greeting budget |
| Waterlink mDNS | an orphan SRV whose owner had the service suffix was treated as discovery without a PTR advertising that instance | `CHECK_waterlink` changes the production PTR to another record while retaining the SRV; PTR and SRV may arrive in either order, but only their validated intersection is exposed |
| Waterlink mDNS | one unauthenticated source could fill all 16 recent-greeting slots with distinct valid instances and starve discovery of an honest new source for ten seconds | `waterlink_service` fills the ring from one address and stops at `LINK_GREET_SOURCE` (8), leaving half the global slots; source rotation remains globally bounded, not solved |
| SNTP | five samples shared one ten-second deadline, so one lost datagram in four ended a query | the `netem` lane's SNTP scenes |
| Supply chain | bowl's Arch, RISC-V Arch and Debian bootstraps rested on TLS and a mirror alone | pinned digests, a pinned signing key for Arch Linux ARM (its signature read through `byte_reader`), `bowl` lane, the signature reader under `bowl_sig_fuzz` |

### Open, in separate branches

| Gap | Branch |
| --- | --- |
| Authenticated time (NTS), a floor for the clock, wget's status 5 for an unverifiable certificate | `feature/clock-floor`, `feature/nts` |
| 802.11w management-frame protection, WPA3-SAE | `feature/wpa3-pmf-sae` |
| DNS over TLS | `feature/dns-over-tls` |
| DHCP over a raw packet socket (so `rp_filter` can be on), the exchange not cut between REQUEST and ACK | `feature/dhcp-packet-socket-acd` |
| The default accepts what wget and curl accept in a header block, URL spelling and redirect statuses | `feature/wget-curl-parity` |
| `edit` sends file bytes to the terminal raw; a secret on a command line is kept by the shell history; boot takes the first install that looks like this build; `fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, `io_uring_disabled` defaults | `hardening/outside-network` |

### Open, with no change planned here

- Plain SNTP is accepted into any moment the build and clock window allow;
  only authenticated time narrows that.
- The DHCP client reads its OFFER from a UDP socket, so `rp_filter` 1 or 2
  drops it; the image keeps `rp_filter` at the kernel's value.
- Every tier refuses an entire body after five minutes even when a peer keeps
  satisfying the 30-second per-read idle timeout. An endless close-delimited
  body can still fill the output disk before that deadline; streaming downloads
  intentionally have no fixed byte ceiling.
- HEAD is not exercised: the client builds only GET.
- A response cut inside its status line or a header stays a failure in both
  tiers. GNU wget exits 0 with nothing written at some cut points and curl
  fails at others, so no tier split matches both, and a cut accepted as
  success would be an empty success.
- `tls_parse_cert` and its raw index reads are guarded by hand, fuzzed and
  judged sound, but are not on `byte_reader`.
- The elements a scan reads are unauthenticated. The join prefers an access
  point that offers what the saved network asks for (WPA2 for one with a
  password, nothing for one without) over a louder one that does not, and
  counts what the real access point's last beacon asked as well as the latest
  frame's, because any station can send a probe response in its name with no
  RSN element in it. That closes the single forged response; an attacker who
  keeps forging the beacons out-races the real ones and can still push the
  real access point behind a twin. The cost is bounded: the order only says
  which access point is tried first, a failed join puts the twin on the avoid
  list, and the handshake is what authenticates. What a twin that is tried
  can take is its first message 2, which any WPA2 station gives to whoever
  sent it a message 1 (an offline guess at the passphrase), as before.
  Held by the `wifi_scan_fuzz` driver (the beacon's elements are kept apart
  from the latest frame's); not held by a join in the `wifi` lane, which has
  no forged-response row.
- The review round before this landed (two readers and four sub-readers, each
  of the Wi-Fi code, the watcher, SNTP, waterlink and the network client; no
  memory-corruption bug a network attacker can reach was found) left these as
  they are, each low and each found by reading:
  - The DNS EDNS0 black-hole is closed here: the EDNS attempt owns only the
    first second of the resolver's absolute budget, and silence is retried
    once without the option. FORMERR and NOTIMP retain the same fallback.
  - Waterlink's per-source admission limiter is now behind cookie proof. An
    unproven address can spend only the global/load buckets, so spoofing a
    paired peer below the load threshold cannot drain that peer's private
    burst; once load triggers cookies, only a mac2 bound to the source address
    and port reaches the per-source limiter. IPv6 rotation is held by the
    global/load buckets rather than creating fresh private bursts. Its refill
    arithmetic also refuses backward clock readings and saturates large
    forward jumps without overflowing the elapsed-time product.
  - A forged clock step, on a clock before the SNTP window, cannot be undone by
    honest answers; authenticated time is the fix (a separate pull request).
  - Waterlink's `leave --forget` now ends a forgotten peer's live sessions as
    soon as that peer proves its identity in its next rekey: all three traffic
    key epochs are scrubbed before another queued carry can be accepted, and
    the next service turn tears down descriptors and children. The cross-group greeting delay is closed here: the recent-greeting
    key includes the group's stable cryptographic mark, while the ring remains
    the global amplification budget. The one-shot mDNS reply's former use of
    the first enumerated interface is closed here: `IP_PKTINFO` binds its A
    record to the local address that received the question.
  - The radio lock is held across a whole join, so `bluetooth add` and
    `remove` wait on a long one. The former first-64 dump-order policy is
    closed: the bounded list keeps the strongest names and the associated
    row, though 64 genuinely stronger forged names can still hide a weaker
    network. The FIFO planted at `/root/wifi` is closed here: saved wifi
    and bluetooth state is opened nonblocking and accepted only as a regular
    file, without following a final symlink.
  - An off-link router in a lease is refused before the lease is installed,
    except with a /32 address where an off-link next hop is an intentional
    point-to-point/cloud configuration. This closes the former four-to-ten
    second acquire/rollback loop.
- The CI `security` job is parked by design and this change does not touch it.

### Decisions

| Id | Decision | Reason |
| --- | --- | --- |
| D01 | No OCSP, CRL, CRLite or Certificate Transparency | Same as default wget and curl; a revoked certificate stays valid until it expires |
| D02 | No common-name fallback | GnuTLS-based wget has one, Chrome none; refusing is deliberate |
| D03 | DNSSEC is out of scope | The resolver is a stub that trusts the network's DNS |
| D04 | Defaults hold what GNU wget 1.25.0 and curl 8.22.0 do for 204 and 205; the RFC 9110 framing is the tight tier's | Real servers send a 204 with `Content-Length: 0`, and the client closes the connection after the one response, so an unread declared body can never be taken for the next response |
| D05 | "Guest is root, not a wall": root is not hardened against itself | Documented policy in `SECURITY.md` |

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
