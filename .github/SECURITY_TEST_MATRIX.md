# Security test matrix

| Threat family | Evidence | Command |
| --- | --- | --- |
| Network byte order, socket boundaries | `socket` lane on each available architecture | `sh test/run socket` |
| Netlink source, sequence, attributes; DNS; HTTP; TLS; DHCP | freestanding network checks | `sh test/run net` |
| Watcher exchange cut by link news; `/32` lease with an off-link router; kernel network defaults of the built image | `net_news_may_cut`, `net_router_onlink` and a kernel `RTNH_F_ONLINK` route on a loopback in `CHECK_net`; the flap and `/32` schedules by hand in a KVM guest with two e1000 on taps and a delayed DHCP server (17.1 s to 8.9 s lease under a 2.5 s flap) | `sh test/run net` (unit half); guest half by hand |
| UDP replay and DHCP reacquisition authorization | identity mutation, queued prior transaction, exhaustive state cross-product | `sh test/run net machine` |
| Certificate path semantics | generated chains (206 rows x key types, 1,282 checks) against OpenSSL and, as a second independent validator, Go's `crypto/x509` (same root, pool, purpose and name; the harness prints where Go and OpenSSL part, 18 rows, and fails wget accepting what Go refuses unless named: Go cannot evaluate directoryName constraints); name-constraints breadth: one-address, all-addresses and excluded ranges, excluded over permitted, case, label boundary, two CAs, mixed leaf names | `python3 test/differential.py --harness tls_chains` |
| Certificate name matching (dNSName wildcards, case, trailing dot, IP, embedded NUL, public-suffix stars) | the lifted `tls_parse_san` against OpenSSL's own hostname check over 51 SAN patterns x 30 hosts, in-memory handshakes; stricter is fine, looser only where named (trailing dot, underscore label under a wildcard) | `python3 test/differential.py --harness tls_hostnames` (via `sh test/run net`) |
| Certificate validity times (UTCTime/GeneralizedTime, the 2049/2050 rule, leap years, second 60, offsets, fractions, wrong tags) | the lifted `tls_date_value` against RFC 5280 written out in Python over 1,262 spellings; two mutants (the century pivot, the 100-year leap rule) caught | `python3 test/differential.py --harness tls_dates` (via `sh test/run net`) |
| Wildcard public suffixes | the lifted lookup against `src/net/suffixes.inc` over every rule-derived name; regeneration proves the compact test equals the Public Suffix List algorithm | `python3 test/differential.py --harness public_suffixes` (via `sh test/run net`) |
| Wycheproof's own vectors under the production crypto (ECDSA through the CertificateVerify DER parse, ECDH raw points, X25519, RSA PKCS#1/PSS, AES-GCM) | 5,384 vectors from C2SP/wycheproof `testvectors_v1`, 7,892 checks on each of three machines | `python3 test/differential.py --harness crypto_vectors --wycheproof DIR` (files fetched by hand; needs network; no lane) |
| wget's status for an unverifiable certificate | every refused chain of `tls_chains` must exit 5 with GNU's wording, 'has expired' and 'is not yet activated' for the dates | `python3 test/differential.py --harness tls_chains` |
| Real-server chains | `tls_verify_chain` against `openssl verify` over 643 public hosts, caIssuers fetched for both; needs the network | `python3 test/differential.py --harness x509_corpus --work DIR` (by hand) |
| TLS 1.3 record layer and state machine with real crypto | a scripted TLS 1.3 server (tickets, KeyUpdate, CCS placements, padding and 2^14 edges, bad tags, alerts, truncation, unasked EncryptedExtensions, CertificateRequest, each key-share group) served to wget and to OpenSSL's client; RFC 8446 column with named deliberate and lenient disagreements | `python3 test/differential.py --harness tls_peer` (via `sh test/run net`) |
| HTTPS→HTTP redirect downgrade | TLS loopback 302 Location shapes under wget manners (9 checks: plain/`http://` sticky+nested, authority-only; uppercase/`//` stay HTTPS; credentialed/`http:///` refuse without silent fetch; no Refresh/meta path) | `python3 test/differential.py --harness https_downgrade` |
| HTTP response framing and delivery (chunked, TE/CL, headers, trailers, 204/205/304) | written MUST_ACCEPT/MUST_REFUSE matrix (114 cases, both `MOONWATER_STRICT` tiers) with `http.client` and curl oracles, and 4,000 generated heads (status, reason byte, Content-Length fields and lists, Transfer-Encoding, Location, folds) held at each tier to a model written from the structure that made them; the default takes what wget and curl both take (identical duplicate Content-Length, `3, 3`, a fold after an ordinary field, a reason-phrase control byte) and the tight tier refuses it; `wget_mutation` serves 63 exact write schedules (one byte at a time with Nagle off and each write acked, splits at grammar boundaries, clean and cut FIN, RST, 1xx storms before 200/204/205, 204/205/304 with every framing, bytes pipelined after a 204, dup/conflicting Content-Length, obs-fold, control bytes in the reason phrase) to the built shell's wget, GNU wget and curl and requires the answers to agree unless the row is named stricter | `python3 test/differential.py --harness http_response_framing`; `python3 test/differential.py --harness wget_mutation`; `python3 test/differential.py --harness http_fuzz_tight` (all in `sh test/run net`); the tight tier's `CHECK_net` is the `net-tight-<host arch>` tally |
| wget's behaviour against scripted servers (request lines, redirects, saved files, exit statuses) | `wget_hostile`: each case is a server script, a URL as a person types it and what must be true; GNU wget and curl run the same cases where the claim is "as they do", and the cases where this client refuses what both fetch are named `DELIBERATE`; `--tight-shell` runs it against a shell built with `MOONWATER_STRICT` 2; covers Location and typed-URL percent-encoding (space, backslash, non-ASCII), inet_aton spellings of an address (127.1, 0x7f.1, 2130706433), a 3xx with no Location, a redirect chain and loop, the deliberate refusals (userinfo, `%00`, backslash in the authority), and in `unshare -Urn` with 8.8.8.8 on `lo` the tight tier's public-to-inside redirect refusal against the default's follow (skipped, and said so, where `unshare` or `ip` is missing) | `python3 test/differential.py --harness wget_hostile` (via `sh test/run net`) |
| URL splitting, Location resolution, output names | `http_split_into` / `http_absolutize` / `http_path_simplify` lifted from net.c against Python's `urllib.parse` over a grammar; `DELIBERATE` names what this client refuses that urllib takes; `http_url_escape` (whole URL and reference, exact room accepted, one byte less refused) against a regular-expression oracle and `http_ipv4_legacy` against glibc `inet_aton` (through Python) with the address's `http_address_public` verdict against an `ipaddress` range list, all in the ASan/UBSan lift | `python3 test/differential.py --harness http_urls` (via `sh test/run net`) |
| Uninitialized wire / ABI padding (MSan) | pad proves (wire + nlattr-shaped) + hosted lifts (`dns_copy_name`, TLS record header, HTTP header/chunk, `dhcp_walk`, `netlink_find_span`, TLS ext/cert without fuzzer) on initialized hostile buffers with per-surface uninit catches + thin CHECK_net-equivalent probes + TLS DER/HS seed smoke under MemorySanitizer; not full CHECK_net; NOT RUN without clang MSan; not CI push | `sh test/run msan` |
| SNTP nonce, ancillary timestamps, timing arithmetic, server selection | machine checks | `sh test/run machine` |
| SNTP across the NTP era boundary (2036 wrap, 2038, 2104) | the lifted `sntp_reply_sample` over exchanges with a known offset at 13 instants, server equal and an hour either side, at the shipped window (taken exactly inside it, refused outside) and with the window moved to the end of era 1 (true offset everywhere); mutant without the era step caught | `python3 test/differential.py --harness sntp_era` (via `sh test/run net`) |
| DHCP and SNTP clients under adversarial scheduling | as built, in a user namespace (`--map-auto`) with a veth pair and netem (delay, jitter, 25% loss for DHCP, duplication, reordering) on both ends and a Python server that answers clean, tripled, forged (wrong transaction id or origin), late, not at all, or with a NAK: the real server's address, route and resolver and never a forged one, at rp_filter 0, 1 and 2; SNTP takes the true answer, refuses a lone server three hours off and a RATE kiss; a server that drops two requests is pinned as a known open row (one ten second budget for five samples); NOT RUN with a reason where namespaces, veth or netem are missing | `sh test/run netem` (`python3 test/differential.py --harness net_netem --shell PATH`) |
| Shell parsing, expansion, environment, status and effects | generated Bash/Dash comparison | `sh test/run shell builtins` |
| File traversal, symlink and replacement behavior | effect-based coreutils differential | `sh test/run files` |
| Tar paths and archive framing | tar lane and compression grammar | `sh test/run tar compression` |
| Pathname race (file/dir/symlink-to-dir exchange) | continuous renameat2/rename scheduler on flip/parent/mid/deep under tar extract (leaf/nested/burst/deep trees) + O_EXCL leaf, private-edit dirfd+O_EXCL, shell noclobber, install -D; outside victim + empty keep/ effect checks; NOT RUN without threads/rename; longer local via `MOONWATER_PATHNAME_RACE_ROUNDS` / `MOONWATER_PATHNAME_RACE_SECONDS` | `python3 test/differential.py --harness pathname_race --binary ours=$shell` (via `sh test/run tar`) |
| Codec hostile lengths and guard pages | codec-specific and floor lanes | `sh test/run codec compression_floor` |
| Waterlink replay, seal and lossy delivery (separate review scope) | pure transform and namespace integration | `sh test/run waterlink link` |
| Kernel module (ring 0): who may read /dev/spark's answers, no lock held across a copy to a caller, every request number equal to the encoding of its struct | `core_state`: capability rows for the script text, bound lines, the input reports and the settings, a lock watch in the copy mock over `snapshot_lock`, `machine_script_lock` and `settings_lock`, zeroed report structs over a poisoned stack, the `IOCTL_IS` table compiled over the real structs and again with a number changed | `python3 test/differential.py --harness core_state` (kit lane) |
| Kernel module (ring 0): a program's window pages charged to it, its log lines rate limited, its shared page read once | `pane_pages` (allocation flags, rate limit), `pane_restride` (double fetch, log), `shared_page` (no plain read or store of `shared->` outside `READ_ONCE`/`WRITE_ONCE`/acquire/release, and a self-test that plants one), `console_queue`, `term_streams` | `python3 test/differential.py --harness pane_pages` (and the others; kit lane) |
| Kernel module (ring 0): a hostile local program on the booted kernel: windows written field by field from many threads, every request number with shaped arguments, spark images with hostile headers, root turning the compositor off and on under live windows, escape sequences into the kernel log; the sanitizers' verdict | `ring0_hostile` (`CHECK_ring0_hostile`) on a stick in a KVM guest built with `kernel/profile/sec_sanitize` (KASAN, UBSAN, lockdep, atomic sleep, hung task) or `sec_kcsan` (a race between the module's own accesses fails it), and with `MOONWATER_RING0_FAIL=N` failing N percent of the program's kernel allocations; a plain image is checked for panic, BUG, oops and warning | `MOONWATER_IMAGE=dist/bootx64.efi sh test/run ring0` (asked for by name) |
| Whole available suite | all locally supported lanes | `sh test/run` |
| TLS DER / handshake, DNS and netlink fuzz (lane smoke) | bounded libFuzzer ASan/UBSan; soft NOT RUN without clang fuzzer | `sh test/run net` (via `tls_der_fuzz` / `tls_hs_fuzz` / `dns_fuzz` / `netlink_fuzz`, 20k/5s) |
| Net fuzz continuous (local) | same harnesses plus `tls_verify_fuzz`; longer budget | `sh test/run fuzz` (`MOONWATER_FUZZ_*`) |
| TLS net fuzz deeper campaign (local) | same harnesses; bounded deeper-than-smoke | `MOONWATER_FUZZ_SECONDS=120 MOONWATER_FUZZ_RUNS=200000 MOONWATER_FUZZ_REPORT=artifacts/fuzz-campaign-report.txt sh test/run fuzz` |
| Release fuzz attach | machine-readable sanitizer + corpus inventory + run exits | `MOONWATER_FUZZ_REPORT=artifacts/fuzz-report.txt sh test/run fuzz` |
| Bowl bootstrap downloads: pinned digest or pinned signing key, refusal on a mismatch | `downloads()` in `CHECK_bowl`: right bytes, one byte short or long, empty, one bit changed, another digest's bytes, no digest (refused), and for the Arch Linux ARM signature gpg's own (RSA-2048 throwaway key, SHA-256 and SHA-512): good, one second under the floor (replay), another key, archive altered, every signature byte flipped in turn, packet cut at every seventh length, a byte after it; every row has a digest or a key and the ARM key's numbers make its fingerprint. By hand, on the box: all five dated/commit URLs fetched with the built wget hash to their pins, and the real 829 MB Arch Linux ARM tarball verifies with its real `.sig` | `sh test/run bowl` |
| Secrets on a command line (`wifi add SSID PASS`, `link join NAME SECRET`) | `history_secret_line` in `CHECK_bowl` (quoting, `;`/`&&`, full path, the kept forms); `link join NAME -` in `moonwater_cli` | `sh test/run bowl cli` |
| Terminal emulator and bowl JSON reader under coverage-guided fuzzing | `term_fuzz` (grid, write cuts, resizes, keys, pointer between writes; cursor-on-the-grid invariant; 720 s clean) and `bowl_json_fuzz` (620 s clean), ASan/UBSan; `term_fuzz` fails on a planted cursor-clamp bug | `python3 test/differential.py --harness term_fuzz` and `--harness bowl_json_fuzz` (smoke in `term`, `bowl`) |
| `edit` sends no file byte a terminal would act on | the `hostile` section of the edit lane: escape, title, bell, DEL, lone C1, C1 in UTF-8, name with an escape or a C1, the file saved as it was | `sh test/run edit` |
| Boot takes an install without asking only when it is this machine's (the disk the session started from, or one on a fixed bus) | `boot_links()` in `CHECK_bowl` classifies sysfs paths (USB, Thunderbolt, NVMe, SATA, mmc, virtual) and `boot_choice()` the pick (this session's disk outranks an earlier-enumerated internal one, an external or other-build disk is never taken, tight takes the medium match only); the install lane (117 rows) still takes an internal NVMe install without asking and asks about another build. Not shown in a guest: a USB install is found only when its enumeration beats the census (racy in QEMU), and the `removable` files of a real PCI port need hardware | `sh test/run bowl install` |
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
| TLS 1.3 client protocol | `tls_hs` | `tls_hs_fuzz` (10 minutes clean, 95 seeds) | the whole record layer and handshake/application state machine (`tls_connect` through `tls_read_until`, crypto and certificate verdict stubbed, AEAD identity) over a fuzzed server stream in PRNG-sized reads (magic F3), with offset, lent-span and stays-closed asserts; framing walks over `tls_handshake_one_append` / `tls_encrypted_flight_append` (F1/F2) |
| DHCP replies and lease clock | `dhcp` | `dhcp_fuzz` (10 minutes clean, 33 seeds) | `dhcp_read` / `dhcp_walk` against an RFC 2131/2132/3396 reference, `dhcp_lease_timers` / `dhcp_lease_acknowledge`, and the watcher's `net_lease_*` clock at fuzzed start and now |
| DNS resolver | `dns` | `dns_fuzz` | net.c's DNS section and wait.c whole over a socket shim serving the input: UDP junk discard, TC to TCP framing, resolv.conf, fault bits; each datagram also as its own question through `dns_reply_result` |
| Wi-Fi scan dump | `wifi_scan` | `wifi_scan_fuzz` | host.c's `radio_bss_read` / `radio_rsn_security` / `radio_air_seen` over net.c's netlink attribute walk: a beacon's information elements and the BSS nest around them, as the bytes of whatever radio is in range; a well-formed beacon is held to a model built from its elements the long way |
| rtnetlink walk | `netlink` | `netlink_fuzz` | net.c's netlink section, wait.c, ip's name table and link/addr/route lines: dump walk sender/port/sequence, DONE/ERROR status, DUMP_INTR, discovery preferences, acks; each message also through every visitor |
| HTTP client | `http` | `http_fuzz`, `http_fuzz_tight` | the whole HTTP section of net.c over a scripted plaintext and TLS transport in PRNG-sized reads, the same 409 seeds at `MOONWATER_STRICT` 1 and 2 (the tight run is `-DMOONWATER_STRICT=2`): every final response is also parsed whole and the streaming verdict and body must equal it (the tight tier's zero-capacity 205 store is part of that model), every request written is held to one request line and four fields, URL split/absolutize/put invariants, chunked bodies streamed and whole; 10 minutes of ASan/UBSan clean at each tier |

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

Length, item-count, and recursion ceilings already enforced in `src/net/net.c`,
with exact-limit and/or one-over proving checks under `CHECK_net`. The one
CPU-work budget is DNS's CNAME limit (`DNS_CNAME_HOPS`), which caps the
record-walk passes a hostile reply can buy; DNS decompression bounds jumps
structurally (ceiling lowers), and DHCP junk discard is an absolute deadline. The full
mapping (parser → ceiling kind → constant → check name) is the comment ledger
at the top of the `CHECK_net` section in `test/checks.c`.

| Parser | Ceiling | Constant | Hit coverage |
| --- | --- | --- | --- |
| DNS | message length | `DNS_MAX_MESSAGE` | exact + one-over unit; TCP/UDP transport one-over |
| DNS | label / name | 63 / 255 | exact + one-over |
| DNS | CNAME hops | `min(answers, DNS_CNAME_HOPS)` + 1, 16 links | chain accept + cycle exhaust; 16 links accept, 17 refuse |
| DNS | resolv.conf servers | `DNS_SERVERS_MAX` (3, glibc MAXNS) | five lines ask three; a cut last line asks none |
| TLS | record payload | `TLS_RECORD_MAX` | exact + one-over (+ empty) |
| TLS | enc plaintext | `TLS_RECORD_MAX - 17` | one-over (+ wraparound) |
| TLS | handshake hold | `TLS_HS_MAX` | exact + one-over |
| TLS | cert extensions / chain | 64 / `certs[8]` | exact + one-over |
| HTTP | URL / headers / body | `HTTP_URL_MAX` / `HTTP_HEAD_MAX` / `HTTP_FETCH_MAX` | exact + one-over |
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
for the response parser: a written MUST_ACCEPT / MUST_REFUSE matrix (97 cases,
452 checks at tip, run once per `MOONWATER_STRICT` tier) held against the
in-tree framing and chunk decoder, with `http.client` and curl as oracles.
204 and 205 are accepted as wget and curl take them (both ignore a 204's
declared body and print a 205's); the tight tier (`TIGHT_REFUSES`) refuses a
204 that declares any body framing and a 205 with a non-zero length. Rows where Moonwater refuses (or accepts a
different body) while `http.client` is looser — TE/CL conflicts, duplicate
framing fields, obs-fold, embedded controls, non-chunked TE, trailer and
chunk-extension grammar, informational framing, leftover after the final
chunk — are named deliberate. Duplicate Date/Host remain accepted (not
framing-critical). HTTPS→HTTP downgrade (`--harness https_downgrade`, 9/9)
exercises wget manners on a TLS loopback for plain and nested `http://`
Locations, sticky secure across hops, uppercase/`//` shapes that stay on
HTTPS, and credentialed/`http:///` Locations that refuse without a silent
fetch. Refresh/meta are not redirect inputs in this tree.
