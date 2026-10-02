# Security test matrix

| Threat family | Evidence | Command |
| --- | --- | --- |
| Network byte order, socket boundaries | `socket` lane on each available architecture | `sh test/run socket` |
| `net.c` dependency closure | exact manifest of shared memory/string/cursor/allocation/byte-order/socket/deadline primitives, with every evidence lane registered and invoked; a new direct dependency fails the gate | `python3 test/differential.py --harness net_dependency_closure` (at the start of `sh test/run net`) |
| Machine-checked `net.c` arithmetic lemmas | all 2^32 DHCP masks by symbolic bit-vector states; all 65,536 AES field products and all 256 S-box values against independent definitions; HKDF's full output-length domain; netlink wire-width/alignment boundaries | `python3 test/differential.py --harness net_math_proof` (at the start of `sh test/run net`) |
| Network deadline clock faults | production `wait.c` compiled over a hostile monotonic clock: zero/backward/forward jumps, exact expiry, subsecond remainder, invalid nanoseconds and saturation at integer overflow | `python3 test/differential.py --harness net_clock_fault` (at the start of `sh test/run net`) |
| Netlink source, sequence, attributes and unrelated-datagram work ceiling; DNS; HTTP; TLS; DHCP | freestanding network checks and `netlink_fuzz`; a transaction accepts at most `NETLINK_DISCARD_MAX` datagrams carrying no matching kernel message | `sh test/run net` |
| SNTP faults and receive-work ceiling | malformed and short replies, lying ancillary lengths, error-queue confusion, NTP eras, clock discontinuities, entropy failure, and at most `SNTP_DISCARD_MAX` non-terminal receives per exchange | `python3 test/differential.py --harness sntp_fuzz`; `python3 test/differential.py --harness sntp_era`; `sh test/run net` |
| TLS and redirect hostile scheduling | seeded TLS handshake/record read chunking plus live HTTPS redirect responses delivered whole, one byte at a time, and immediately around every CR/LF/colon grammar edge on every hop | `python3 test/differential.py --harness tls_hs_fuzz`; `python3 test/differential.py --harness https_downgrade` |
| UDP replay and DHCP reacquisition authorization | identity mutation, queued prior transaction, exhaustive state cross-product | `sh test/run net machine` |
| Watcher exchange cut by link news; kernel network defaults of the built image | `net_news_may_cut` in `CHECK_net` (a carrier flap cuts the exchange once, then the four-second hold-off); the flap schedule by hand in a KVM guest with two e1000 on taps and a delayed DHCP server (a lease 17.1 s after the start became 8.9 s under a 2.5 s flap, measured before this change was split out of the larger one) | `sh test/run net` (unit half); guest half by hand |
| Lease sanity (address, server, router, prefix) | `dhcp_lease_usable` over a grid of addresses, masks and routers against the written rule (a unicast address, an on-link router except for a /32, a prefix clear of 0/8, 127/8 and 224/3) | `sh test/run net` |
| Resolver: source port, 0x20, EDNS0, order, first-answer rule and junk-work ceiling | a forked reporting server in `CHECK_net`, including FORMERR and silent EDNS black-hole fallbacks within one absolute budget, and exactly 64 wrong-identity datagrams accepted before a valid neighbor while the 65th fails closed; two real servers on 127.0.0.2 and 127.0.0.3 in a namespace | `sh test/run net` |
| Kernel network defaults of the image | the boot lane's row reads five of them from the booted guest (redirects, router advertisements, autoconf, RFC 1337, SYN cookies); `net_sysctl` cuts the writer and its directory walk out of `src/sh/net.c` and runs it over seventy interface directories (every leaf written, a dotted name and a name too long for the path left alone; the one-read walk it replaced missed 49 of 71) | `sh test/run boot`; `python3 test/differential.py --harness net_sysctl` (kit lane) |
| SYN cookies under a flood | by hand: a spoofed-source SYN flood at a guest listener with cookies on and off | recipe in `SECURITY_REVIEW.md` |
| Certificate path semantics | generated chains against OpenSSL and, as a second independent validator, Go's `crypto/x509`; Mozilla's `distrust-after` dates per anchor | `python3 test/differential.py --harness tls_chains` |
| Certificate name matching (dNSName wildcards, case, trailing dot, IP, embedded NUL, public-suffix stars) | the lifted `tls_parse_san` against OpenSSL's own hostname check over 51 SAN patterns x 30 hosts, in-memory handshakes; stricter is fine, looser only where named (trailing dot, underscore label under a wildcard) | `python3 test/differential.py --harness tls_hostnames` (via `sh test/run net`) |
| Certificate validity times (UTCTime/GeneralizedTime, the 2049/2050 rule, leap years, second 60, offsets, fractions, wrong tags) | the lifted `tls_date_value` against RFC 5280 written out in Python over 1,262 spellings; two mutants (the century pivot, the 100-year leap rule) caught | `python3 test/differential.py --harness tls_dates` (via `sh test/run net`) |
| Wildcard public suffixes | the lifted lookup against `src/net/suffixes.inc` over every rule-derived name; regeneration proves the compact test equals the Public Suffix List algorithm | `python3 test/differential.py --harness public_suffixes` (via `sh test/run net`) |
| Production crypto and `lib.c` architecture bodies | generated OpenSSL differential vectors for SHA-1/256/384, HMAC, HKDF, PBKDF2, AES-128-GCM, X25519, P-256/P-384 ECDH/ECDSA and RSA PKCS#1/PSS; production `crypto_*` composition and every available `lib.c` assembly body, including x86-64 mulx and mulq; branchless-body and X25519 instruction-count checks; the same composition under libFuzzer against libcrypto | `python3 test/differential.py --harness crypto_vectors` through `sh test/run net`; `python3 test/differential.py --harness crypto_fuzz` |
| Wycheproof's own vectors under the production crypto (ECDSA through the CertificateVerify DER parse, ECDH raw points, X25519, RSA PKCS#1/PSS, AES-GCM) | 5,384 vectors from C2SP/wycheproof `testvectors_v1`, 7,892 checks on each of three machines | `python3 test/differential.py --harness crypto_vectors --wycheproof DIR` (files fetched by hand; needs network; no lane) |
| Real-server chains | `tls_verify_chain` against `openssl verify` over 643 public hosts, caIssuers fetched for both; needs the network | `python3 test/differential.py --harness x509_corpus --work DIR` (by hand) |
| TLS 1.3 record layer and state machine with real crypto | a scripted TLS 1.3 server (tickets, KeyUpdate, CCS placements, padding and 2^14 edges, bad tags, alerts, truncation, unasked EncryptedExtensions, CertificateRequest, each key-share group) served to wget and to OpenSSL's client; RFC 8446 column with named deliberate and lenient disagreements | `python3 test/differential.py --harness tls_peer` (via `sh test/run net`) |
| HTTPS→HTTP redirect downgrade | TLS loopback 302 Location shapes under wget manners (9 checks: plain/`http://` sticky+nested, authority-only; uppercase/`//` stay HTTPS; credentialed/`http:///` refuse without silent fetch; no Refresh/meta path) | `python3 test/differential.py --harness https_downgrade` |
| Hostile servers and redirect shapes against the built wget (request line by request line; Location injection, loops, userinfo, header floods, the address policy of the tight tier) | scripted servers, GNU wget and curl run beside the built wget wherever the claim is "as they do" | `python3 test/differential.py --harness wget_hostile` (via `sh test/run net`) |
| DHCP and SNTP clients under adversarial scheduling | `ip auto` and `moonwater time sync` against a server that triples, forges, delays, drops and NAKs its answers, plain and under netem loss, duplication and reordering, in a user and network namespace pair; DHCP additionally has exact/one-over wrong-datagram work-budget rows; NOT RUN with a reason where namespaces, veth or netem are missing | `sh test/run netem` |
| NTP era arithmetic | `sntp_reply_sample` lifted from `host.c` and asked about exchanges whose true offset spans the 2036 wrap, 2038 and 2104, at the shipped window and with the window moved to the end of era 1 | `python3 test/differential.py --harness sntp_era` (via `sh test/run net`) |
| HTTP response framing and delivery (chunked, TE/CL, headers, trailers) | written MUST_ACCEPT/MUST_REFUSE matrix with `http.client` and curl oracles, at the default and the tight tier (`MOONWATER_STRICT` 2: the RFC 9110 refusals of a 204 that declares a body and a 205 with content); real wget receives length and chunked bodies one byte at a time and across every grammar boundary, clean/cut FIN and RST, and a 32-response 1xx storm, GNU wget and curl run beside it over the same schedules | `python3 test/differential.py --harness http_response_framing`; `python3 test/differential.py --harness wget_mutation`; the whole client over a scripted transport under libFuzzer, built at both tiers (at the tight tier no connection goes inside after one to public space): `python3 test/differential.py --harness http_fuzz` |
| HTTP cross-layer destination identity | per-hop transcript requires parsed/resolved hostname → returned IPv4 → connected address/port → TLS hostname → serialized Host authority to agree; redirect hops replace the transcript rather than retaining the prior identity; default and tight tiers | `python3 test/differential.py --harness http_fuzz` |
| Uninitialized wire / ABI padding (MSan) | pad proves (wire + nlattr-shaped) + hosted lifts (`dns_copy_name`, TLS record header, HTTP header/chunk, `dhcp_walk`, `netlink_find_span`, TLS ext/cert without fuzzer) on initialized hostile buffers with per-surface uninit catches + thin CHECK_net-equivalent probes + TLS DER/HS seed smoke under MemorySanitizer; not full CHECK_net; NOT RUN without clang MSan; not CI push | `sh test/run msan` |
| SNTP nonce, ancillary timestamps, timing arithmetic, server selection and exact datagram framing | machine checks; a 48-byte datagram is accepted while a 49-byte datagram read through the production `recvmsg` helper is refused rather than prefix-truncated | `sh test/run machine` |
| DHCP address ownership before installation | three ARP probes from 0.0.0.0 on the selected interface; ARP requests, replies and simultaneous probes from another MAC conflict; raw-socket/bind/send/receive/deadline failures refuse installation | `sh test/run storage_io`; parser rows in `storage_test_arp_claims` |
| Shell parsing, expansion, environment, status and effects | generated Bash/Dash comparison | `sh test/run shell builtins` |
| File traversal, symlink and replacement behavior | effect-based coreutils differential | `sh test/run files` |
| Tar paths and archive framing | tar lane and compression grammar | `sh test/run tar compression` |
| Pathname race (file/dir/symlink-to-dir exchange) | continuous renameat2/rename scheduler on flip/parent/mid/deep under tar extract (leaf/nested/burst/deep trees) + O_EXCL leaf, private-edit dirfd+O_EXCL, shell noclobber, install -D; outside victim + empty keep/ effect checks; NOT RUN without threads/rename; longer local via `MOONWATER_PATHNAME_RACE_ROUNDS` / `MOONWATER_PATHNAME_RACE_SECONDS` | `python3 test/differential.py --harness pathname_race --binary ours=$shell` (via `sh test/run tar`) |
| Codec hostile lengths and guard pages | codec-specific and floor lanes | `sh test/run codec compression_floor` |
| Waterlink replay, seal, discovery grammar and lossy delivery (separate review scope) | pure transform and namespace integration; mDNS PTR/ANY questions and SRV records act only in Internet class, with QU/cache-flush accepted and CHAOS ignored; query RCODE is zero and a zero-TTL SRV goodbye cannot become a live endpoint; an SRV target is a complete non-root DNS name with a nonzero port, and the instance must also be advertised by an exact service PTR before it can spend greeting work; the hosted sanitizer model generates valid pairs in both orders, repeats and bounded-table overflow | `sh test/run waterlink link`; `python3 test/differential.py --harness waterlink_sanitized` |
| Waterlink authorization revocation | remove a live peer's authorization, deliver its next authenticated rekey, require current/next/grace traffic keys to become immediately unaddressable and the session to be torn down without an answer | `sh test/run waterlink` (`responder`) |
| Saved wifi and bluetooth lists survive a cut write; two bluetooth runs at once keep both | `moonwater_cli`: RLIMIT_FSIZE 0 kills the writer, a flock holds the radio lock, twelve adds at once | `sh test/run cli` |
| Waterlink handshake flood (mac1 passes, cookie path, admission) and the group file's check | unproven addresses cannot spend a per-source burst; after the load threshold, a source-and-port-bound mac2 is required before the exact per-source burst is available; backward time fails closed and a maximal forward jump saturates without multiply wrap; `waterlink_link` adds 3,000 initiations a second from 900 ports over netem while measuring a paired peer | `sh test/run waterlink link` |
| Waterlink mDNS interface identity and reply/greeting budgets | a unicast question received on the second local address is answered with that address in its A record, not the first enumerated interface; received TTL remains 255-gated; a forged source port zero immediately before an honest one-shot asker is neither answered nor promoted to multicast and does not spend the global successful-reply interval; one unauthenticated source can spend at most eight of sixteen recent-greeting slots, enough for every group but not enough to starve a second source | `sh test/run waterlink` |
| MSan on waterlink | `waterlink_pre_fuzz` and `waterlink_fuzz` under libFuzzer MSan | `MOONWATER_MSAN=1 python3 test/differential.py --harness waterlink_pre_fuzz` (and `waterlink_fuzz`; `MOONWATER_FUZZ_SECONDS`) |
| Bowl bootstrap downloads: pinned digest or pinned signing key, refusal on a mismatch | `downloads()` in `CHECK_bowl`: right bytes, one byte short or long, empty, one bit changed, another digest's bytes, no digest (refused), and for the Arch Linux ARM signature gpg's own (a throwaway RSA-2048 key, SHA-256 and SHA-512): good, one second under the floor (replay), another key, archive altered, every signature byte flipped in turn, a packet cut at every seventh length, a byte after it; `bowl_signature_read` reads the .sig file through `byte_reader` (in `security_hygiene`'s `reader_only`), and the reader it replaced agreed with it on 35 million generated and mutated packets under ASan and UBSan | `sh test/run bowl`; the signature reader under libFuzzer: `python3 test/differential.py --harness bowl_sig_fuzz` (via `sh test/run net`) |
| Kernel module (ring 0): who may read /dev/spark's answers, no lock held across a copy to a caller, every request number equal to the encoding of its struct | `core_state`: capability rows for the script text, bound lines, the input reports and the settings, a lock watch in the copy mock over `snapshot_lock`, `machine_script_lock` and `settings_lock`, zeroed report structs over a poisoned stack, the `IOCTL_IS` table compiled over the real structs and again with a number changed | `python3 test/differential.py --harness core_state` (kit lane) |
| Kernel module (ring 0): a program's window pages charged to it, its log lines rate limited, its shared page read once | `pane_pages` (allocation flags, rate limit), `pane_restride` (double fetch, log), `shared_page` (no plain read or store of `shared->` outside `READ_ONCE`/`WRITE_ONCE`/acquire/release, and a self-test that plants one), `console_queue`, `term_streams` | `python3 test/differential.py --harness pane_pages` (and the others; kit lane) |
| Kernel module (ring 0): a hostile local program on the booted kernel: windows written field by field from many threads, every request number with shaped arguments, spark images with hostile headers, root turning the compositor off and on under live windows, escape sequences into the kernel log; the sanitizers' verdict | `ring0_hostile` (`CHECK_ring0_hostile`) on a stick in a KVM guest built with `kernel/profile/sec_sanitize` (KASAN, UBSAN, lockdep, atomic sleep, hung task) or `sec_kcsan` (a race between the module's own accesses fails it), and with `MOONWATER_RING0_FAIL=N` failing N percent of the program's kernel allocations; a plain image is checked for panic, BUG, oops and warning | `MOONWATER_IMAGE=dist/bootx64.efi sh test/run ring0` (asked for by name) |
| Whole available suite | all locally supported lanes | `sh test/run` |
| TLS DER / handshake, DNS, netlink and bowl signature fuzz (lane smoke) | bounded libFuzzer ASan/UBSan; soft NOT RUN without clang fuzzer | `sh test/run net` (via `tls_der_fuzz` / `tls_hs_fuzz` / `dns_fuzz` / `netlink_fuzz` / `bowl_sig_fuzz`, 20k/5s) |
| Net fuzz continuous (local) | same harnesses plus `tls_verify_fuzz`; longer budget | `sh test/run fuzz` (`MOONWATER_FUZZ_*`) |
| TLS net fuzz deeper campaign (local) | same harnesses; bounded deeper-than-smoke | `MOONWATER_FUZZ_SECONDS=120 MOONWATER_FUZZ_RUNS=200000 MOONWATER_FUZZ_REPORT=artifacts/fuzz-campaign-report.txt sh test/run fuzz` |
| Release fuzz attach | machine-readable sanitizer + corpus inventory + run exits | `MOONWATER_FUZZ_REPORT=artifacts/fuzz-report.txt sh test/run fuzz` |
| Release architecture gate | x86-64, ARM64 and RISC-V become required rather than soft `NOT RUN`; freestanding undefined behavior traps, with hosted ASan/UBSan and MSan reported separately | `MOONWATER_REQUIRE_ARCHES=1 MW_UBSAN=1 sh test/run net`; `sh test/run fuzz`; `sh test/run msan` |
| Security test hygiene (procedural) | harness registration and wiring; soft-skip lanes; seed tables and no seed files in the tree; verify-lift proves; framing matrix consistency | `python3 test/differential.py --harness security_hygiene` (also at start of `lane_net` / `lane_tar`) |

## Fuzz corpora and release publish

Seed bytes come from `tls_fuzz_seeds` in `test/differential.py` (hex
fixtures plus built shapes; `dns_fuzz_seeds` and `netlink_fuzz_seeds` for
the two net corpora). Each run writes them into its own temporary
corpus directory; no seed file lives in the tree.

| Corpus | Seeds | Harness | What it exercises |
| --- | --- | --- | --- |
| DER / Certificate list | `tls_der` | `tls_der_fuzz` | `tls_parse_extensions` / `tls_parse_cert` / certificate-list framing; EKU/SAN/BC/KU value lanes; names_chain + leaf/issuer policy; NameConstraints (CC); magic prefixes C1–C9, CC |
| DER verify + sig | `tls_der` (same) | `tls_verify_fuzz` | `tls_verify_chain` parse/policy/names walker plus production `tls_verify_one` (hosted C montgomery + SHA); WR2→GTS prove in `LLVMFuzzerInitialize`; **not** lane_net smoke (hand / `sh test/run fuzz`) |
| TLS 1.3 client protocol | `tls_hs` | `tls_hs_fuzz` | the whole record layer and handshake/application state machine (`tls_connect` through `tls_read_until`, crypto and certificate verdict stubbed, AEAD identity) over a fuzzed server stream in PRNG-sized reads (magic F3), with offset, lent-span and stays-closed asserts; framing walks over `tls_handshake_one_append` / `tls_encrypted_flight_append` (F1/F2) |
| DHCP replies and lease clock | `dhcp` | `dhcp_fuzz` | `dhcp_read` / `dhcp_walk` against an RFC 2131/2132/3396 reference, `dhcp_lease_timers` / `dhcp_lease_acknowledge`, and the watcher's `net_lease_*` clock at fuzzed start and now |
| DNS resolver | `dns` | `dns_fuzz` | net.c's DNS section and wait.c whole over a socket shim serving the input: UDP junk discard, TC to TCP framing, resolv.conf, fault bits; each datagram also as its own question through `dns_reply_result` |
| Wi-Fi scan dump | `wifi_scan` | `wifi_scan_fuzz` | host.c's `radio_bss_read` / `radio_rsn_security` / `radio_air_seen` over net.c's netlink attribute walk: a beacon's information elements and the BSS nest around them, as the bytes of whatever radio is in range; a well-formed beacon is held to a model built from its elements the long way; `wifi_air_capacity` fills all 64 rows with weak distinct names, admits a stronger real row, refuses a weaker extra row, retains an associated row regardless of signal, and proves its joined bit/BSSID/security/channel cannot be spliced with a louder same-name twin |
| rtnetlink walk | `netlink` | `netlink_fuzz` | net.c's netlink section, wait.c, ip's name table and link/addr/route lines: dump walk sender/port/sequence, DONE/ERROR status, DUMP_INTR, discovery preferences, acks; each message also through every visitor |
| bowl's OpenPGP signature | `bowl_sig` | `bowl_sig_fuzz` | bowl.c's `bowl_signature_read`, `bowl_signature_ok` and the key routines under them (`bowl_key_modulus`, `bowl_key_mpi`, `bowl_key_fingerprint`) over net.c's hosted crypto, so the RSA check is the production one: the `.sig` a mirror serves as any bytes, as a good packet with its unhashed area replaced (which has to be taken), with bytes changed in a signed field, cut or followed by junk, or with another integer behind the real check bytes (each of which has to be refused), as a body framed with a header that says its length, in pieces and with reads that fail; a key written as the pinned one is and then bent. The signatures it starts from are made in Python with throwaway 2048 and 4096 bit keys, and each is held to be taken or refused for the reason it was built for before the fuzzing begins |

Seed counts are whatever `tls_fuzz_seeds` returns; the fuzz lane's report
records them.
**lane_net** leaves `MOONWATER_FUZZ_*` unset (20 000 runs / 5 s smoke). Harnesses
print `N seeds, libFuzzer ASan/UBSan (-runs=… -max_total_time=…) clean` on
success, or exit 2 (NOT RUN) when clang cannot link `-fsanitize=fuzzer`.
They do not emit LLVM source-line coverage; `-print_final_stats=0`.

**Deeper local campaign (manual, no CI; does not change lane_net smoke):**

```sh
MOONWATER_FUZZ_SECONDS=120 MOONWATER_FUZZ_RUNS=200000 \
MOONWATER_FUZZ_REPORT=artifacts/fuzz-campaign-report.txt sh test/run fuzz
```

Budget is deeper than the 20k/5s lane smoke and shorter than the default
one-hour continuous `sh test/run fuzz`. Reports under `artifacts/` are
gitignored; do not check in large campaign dumps.

**Release attach (manual, no CI automation):**

1. `MOONWATER_FUZZ_REPORT=artifacts/fuzz-report.txt sh test/run fuzz` — smoke
   budget unless `MOONWATER_FUZZ_RUNS` / `MOONWATER_FUZZ_SECONDS` already
   set; longer: e.g. `MOONWATER_FUZZ_SECONDS=3600` (or the deeper campaign
   above).
2. Attach `artifacts/fuzz-report.txt` (JSON: clang version, seed counts,
   budget, per-target duration/exit, `overall_exit`, and the commit whose
   `tls_fuzz_seeds` made the seeds).
3. `overall_exit` 0 = both clean; 1 = sanitizer/fail; 2 = NOT RUN on this
   host (still publish the report — it records the toolchain gap).

## Required test shapes for a new parser

Every new externally reachable parser must add:

1. empty, shortest, exact-limit, and one-over-limit objects;
2. truncation at every byte boundary;
3. every discriminator/tag value, including unknown values;
4. integer encodings at every width and overflow;
5. duplicate, reordered, conflicting, and trailing fields;
6. every stream split point and partial read/write behavior;
7. a fixed resource ceiling and a test which reaches it;
8. identity mutations for every field used to authorize a reply;
9. fault injection for allocation, randomness, time, and I/O where applicable;
10. a differential oracle or a documented reason no independent oracle exists;
11. its bytes read through lib.util.c's `byte_reader` (a window whose reads are
    checked and whose first short read fails it for good) rather than an offset
    kept beside the buffer, and its name in `reader_only` in
    `harness_security_hygiene`, which refuses a listed parser that subscripts or
    steps the pointer it was handed; a hosted lift of it takes the cursor with
    `byte_reader_source()` (and `byte_store_source()` for a writer).

Passing rows are evidence for a particular build and environment, not a
permanent certification. Unsupported architecture, namespace, sanitizer, or
oracle lanes must be reported as not run rather than silently counted as pass.

## Per-parser length/item/recursion ceilings (net)

Length, item-count, recursion and transaction-work ceilings already enforced in
`src/net/net.c`, with exact-limit and/or one-over proving checks under
`CHECK_net`. DNS's CNAME limit (`DNS_CNAME_HOPS`) caps record-walk passes,
`DNS_DISCARD_MAX` caps wrong-identity datagrams a readable socket can buy, DNS
decompression bounds jumps structurally (ceiling lowers), and DHCP combines an
absolute deadline with `DHCP_DISCARD_MAX` for invalid datagrams and state-wrong
valid replies. Netlink likewise caps datagrams containing no message for the
transaction while leaving matching multipart dumps bounded by their 16 MiB
datagram ceiling. The full
mapping (parser → ceiling kind → constant → check name) is the comment ledger
at the top of the `CHECK_net` section in `test/checks.c`.

| Parser | Ceiling | Constant | Hit coverage |
| --- | --- | --- | --- |
| DNS | message length | `DNS_MAX_MESSAGE` | exact + one-over unit; TCP/UDP transport one-over |
| DNS | label / name | 63 / 255 | exact + one-over |
| DNS | CNAME hops | `min(answers, DNS_CNAME_HOPS)` + 1, 16 links | chain accept + cycle exhaust; 16 links accept, 17 refuse |
| DNS | wrong-identity datagrams per query | `DNS_DISCARD_MAX` (64) | exact budget followed by a valid answer; one over fails closed |
| DNS | resolv.conf servers | `DNS_SERVERS_MAX` (3, glibc MAXNS) | five lines ask three; a cut last line asks none |
| DHCP | invalid/state-wrong replies per receive phase | `DHCP_DISCARD_MAX` (64) | exact invalid-datagram budget followed by a valid offer; one over fails closed; OFFER and ACK/NAK loops share the same ceiling |
| Netlink | unrelated datagrams per transaction | `NETLINK_DISCARD_MAX` (64) | source/port/sequence model and fuzz invariant; matching multipart dumps do not spend the unrelated budget |
| SNTP | short, irrelevant or non-terminal receives per exchange | `SNTP_DISCARD_MAX` (64) | deadline/clock-fault checks plus malformed reply and ancillary-control fuzz corpus |
| TLS | record payload | `TLS_RECORD_MAX` | exact + one-over (+ empty) |
| TLS | enc plaintext | `TLS_RECORD_MAX - 17` | one-over (+ wraparound) |
| TLS | handshake hold | `TLS_HS_MAX` | exact + one-over |
| TLS | cert extensions / chain | 64 / `certs[8]` | exact + one-over |
| HTTP | URL / headers / body | `HTTP_URL_MAX` / `HTTP_HEAD_MAX` / `HTTP_FETCH_MAX` | exact + one-over |
| HTTP | complete body wall time | `HTTP_BODY_SECONDS` (300 s), every tier | a byte stream faster than the idle timer still expires at an injected 100 ms absolute deadline; default and tight whole-client fuzz lifts both carry the production initialization |
| HTTP (tight) | initial URL identity | every RFC 3986 leading scheme token | `ftp:21`, `smtp:25`, and one-letter `h:81` are schemes, not plaintext host:port shorthand; explicit HTTP authorities remain accepted |
| Waterlink mDNS answer budget | one successful multicast emission per interval | kernel-refused send does not advance `last_answer`; backward time does not wrap the age open |
| Waterlink legacy group migration | every 32-byte verifier field, durably | positional all-write handles `EINTR`/short writes; `/dev/full` refusal activates no migrated groups; `fsync` required |
| Waterlink multicast egress identity | selected ifindex and advertised interface address | real socket sends after loopback selection, then refuses an impossible ifindex instead of using the retained loopback choice |
| Waterlink reliable-link clock ordering | flight and delayed-ack timestamps newer than the caller's snapshot | direct future-timestamp rows plus generated schedules; elapsed comparisons cannot unsigned-wrap into loss/timeout/ack work |
| HTTP | redirects / writev spans | `HTTP_HOPS` / `HTTP_WRITE_SPANS` | hop ceiling; spans+1 flush |
| DHCP | receive room | caller buffer (300 in test) | exact + one-over |

## Mid-path resource / I/O faults (net)

Open-time soft `RLIMIT_NOFILE` remains in `http_tls_resource_exhaustion` and
`dns_dhcp_netlink_resource_exhaustion`. Soft `RLIMIT_AS` → mmap refuse for
HTTP body store / `net_room` is proved only on the native host arch (Lima
aarch64 / Linux arm64 when that lane runs without qemu-user). qemu-user
arches receive `--emulated` from `test/run` and log NOT RUN for AS; they
still prove allocation fail-closed via once-armed `memory_reserve`.

Deeper mid-path fail-closed proofs under `CHECK_net` (not every path):

| Path | Check | Claim |
| --- | --- | --- |
| HTTP body store after connected prefix (AS) | `http_body_store_midpath_exhaustion` | capacity fill then next `byte_store_reserve` under soft `RLIMIT_AS` → `HTTP_NO_REPLY` (native only; `--emulated` / no `prlimit` → NOT RUN) |
| HTTP body store after connected prefix (inject) | `http_body_store_midpath_reserve_fault` | same capacity fill; once-armed `memory_reserve` → `HTTP_NO_REPLY` (every arch) |
| HTTP body store large growth (inject) | `http_body_store_large_growth_reserve_fault` | fill past first quantum; next expansion once-armed refuse → `HTTP_NO_REPLY` |
| HTTP first-reserve (inject + AS) | `http_tls_resource_exhaustion` | once-armed `memory_reserve` → `HTTP_NO_REPLY`; soft AS on native |
| netlink first-reserve (inject + AS) | `dns_dhcp_netlink_resource_exhaustion` | once-armed `memory_reserve` → failed/empty; soft AS on native |
| netlink mid-path growth (inject) | `net_room_midpath_reserve_fault` | successful `net_room` then once-armed refuse → failed |
| HTTP body copy after connected prefix | `http_body_copy_midpath_fault` | writev once-armed `-ENOSPC` → `HTTP_WRITE`, no hang |
| HTTP writev after short prefix | `http_write_spans_midpath_fault` | short writev then once-armed `-ENOSPC` → refuse, no hang |
| TLS HS append after retained prefix | `tls_midpath_append_refusal` | fixed `TLS_HS_MAX` hold room refuses next fragment (no mmap/heap path) |
| DNS TCP frame | (stack only) | `dns_retry_tcp` uses stack `DNS_MAX_MESSAGE`; no `byte_store_reserve` / AS case |
| DNS UDP send after connect | `dns_dhcp_midpath_faults` | once-armed `-ENOSPC` / `-EINTR` → `DNS_NO_REPLY`, no hang |
| DNS UDP recv after readable junk peer | `dns_dhcp_midpath_faults` | `EINTR`→`-EIO`, `-EAGAIN`, short fill→`-ENOSPC` → `DNS_NO_SERVER`; sticky `EINTR`×deadline → `DNS_NO_REPLY` |
| DNS TCP fallback mid-send | `dns_dhcp_midpath_faults` | short send then `-ENOSPC`; `EINTR` then `-ENOSPC` → `DNS_NO_REPLY` |
| DHCP recv on bound socket | `dns_dhcp_midpath_faults` | `EINTR`→`-ENOSPC`, `-EAGAIN`, junk then short/`EIO`, sticky `EINTR`×deadline → refuse, no hang |
| DHCP reacquire send (optional) | `dns_dhcp_midpath_faults` | `-ENOSPC` → `DHCP_NO_SOCKET` when `BINDTODEVICE lo` works; else NOT RUN |

Open-time soft `RLIMIT_NOFILE` for DNS/DHCP/netlink remains in
`dns_dhcp_netlink_resource_exhaustion` (socket open gate only).

## Oracle quality

A differential oracle is not enough by itself: two implementations can agree
on the same mistake. The TLS chain grammar therefore carries an independent
accept/refuse matrix for every mutation and key type as well as comparing the
two implementations. Invalid SAN, validity, CA, path-length, key-usage, EKU,
critical-extension, issuer, and trust-anchor cases must be rejected; valid
depths, absent optional leaf/issuer constraints, non-critical basic constraints,
a served root, unknown non-critical extensions, an inclusive sixty-four
extension ceiling, names inside an intermediate's name constraints, an
RSA-8192 leaf and intermediate, and an intermediate fetched from the leaf's
caIssuers location must be accepted. Duplicate extension OIDs (including
unknown and non-adjacent duplicates) and a sixty-five extension work ceiling
are refused even when OpenSSL accepts them; those rows are named deliberate.
Extension policy is exercised at both leaves and intermediates. Deliberately
stricter Moonwater policy is named at the individual case.

HTTP response framing (`--harness http_response_framing`) is the same shape
for the response parser: a written MUST_ACCEPT / MUST_REFUSE matrix held
against the in-tree framing and chunk decoder, at the default and the tight
tier, with `http.client` and curl as oracles and `wget_mutation` running GNU
wget over the same delivery schedules. Rows where Moonwater refuses (or accepts a
different body) while `http.client` is looser — TE/CL conflicts, duplicate
framing fields, obs-fold, embedded controls, non-chunked TE, trailer and
chunk-extension grammar, informational framing, leftover after the final
chunk — are named deliberate. Duplicate Date/Host remain accepted (not
framing-critical). HTTPS→HTTP downgrade (`--harness https_downgrade`, 9/9)
exercises wget manners on a TLS loopback for plain and nested `http://`
Locations, sticky secure across hops, uppercase/`//` shapes that stay on
HTTPS, and credentialed/`http:///` Locations that refuse without a silent
fetch. Refresh/meta are not redirect inputs in this tree.
