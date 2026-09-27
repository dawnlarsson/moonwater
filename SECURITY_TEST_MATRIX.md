# Security test matrix

| Threat family | Evidence | Command |
| --- | --- | --- |
| Network byte order, socket boundaries | `socket` lane on each available architecture | `sh test/run socket` |
| Netlink source, sequence, attributes; DNS; HTTP; TLS; DHCP | freestanding network checks | `sh test/run net` |
| UDP replay and DHCP reacquisition authorization | identity mutation, queued prior transaction, exhaustive state cross-product | `sh test/run net machine` |
| Certificate path semantics | generated chains against OpenSSL | `python3 test/differential.py --harness tls_chains` |
| HTTPS→HTTP redirect downgrade | TLS loopback 302 Location shapes under wget manners (9 checks: plain/`http://` sticky+nested, authority-only; uppercase/`//` stay HTTPS; credentialed/`http:///` refuse without silent fetch; no Refresh/meta path) | `python3 test/differential.py --harness https_downgrade` |
| HTTP response framing (chunked, TE/CL, headers, trailers) | written MUST_ACCEPT/MUST_REFUSE matrix (54 cases / 136 checks); http.client as second oracle with named deliberate disagreements | `python3 test/differential.py --harness http_response_framing` |
| Uninitialized wire / ABI padding (MSan) | pad proves (wire + nlattr-shaped) + hosted lifts (`dns_copy_name`, TLS record header, HTTP header/chunk, `dhcp_walk`, `netlink_find_span`, TLS ext/cert without fuzzer) on initialized hostile buffers with per-surface uninit catches + thin CHECK_net-equivalent probes + TLS DER/HS seed smoke under MemorySanitizer; not full CHECK_net; NOT RUN without clang MSan; not CI push | `sh test/msan_net` |
| SNTP nonce, ancillary timestamps, timing arithmetic, server selection | machine checks | `sh test/run machine` |
| Shell parsing, expansion, environment, status and effects | generated Bash/Dash comparison | `sh test/run shell builtins` |
| File traversal, symlink and replacement behavior | effect-based coreutils differential | `sh test/run files` |
| Tar paths and archive framing | tar lane and compression grammar | `sh test/run tar compression` |
| Pathname race (file/dir/symlink-to-dir exchange) | continuous renameat2/rename scheduler on flip/parent/mid/deep under tar extract (leaf/nested/burst/deep trees) + O_EXCL leaf, private-edit dirfd+O_EXCL, shell noclobber, install -D; outside victim + empty keep/ effect checks; NOT RUN without threads/rename; longer local via `MOONWATER_PATHNAME_RACE_ROUNDS` / `MOONWATER_PATHNAME_RACE_SECONDS` | `python3 test/differential.py --harness pathname_race --binary ours=$shell` (via `sh test/run tar`) |
| Codec hostile lengths and guard pages | codec-specific and floor lanes | `sh test/run codec compression_floor` |
| Waterlink replay, seal and lossy delivery (separate review scope) | pure transform and namespace integration | `sh test/run waterlink link` |
| Whole available suite | all locally supported lanes | `sh test/run` |
| TLS DER / handshake fuzz (lane smoke) | bounded libFuzzer ASan/UBSan; soft NOT RUN without clang fuzzer | `sh test/run net` (via `tls_der_fuzz` / `tls_hs_fuzz`, 20k/5s) |
| TLS net fuzz continuous (local) | same harnesses; longer budget | `sh test/fuzz_net` (`MOONWATER_FUZZ_*`) |
| TLS net fuzz deeper campaign (local) | same harnesses; bounded deeper-than-smoke | `sh test/fuzz_campaign` (200k/120s + report; see `test/fuzz_corpus/README.md`) |
| Release fuzz attach | machine-readable sanitizer + corpus inventory + run exits | `sh test/fuzz_net --report` → `artifacts/fuzz-report.txt` |
| Security test hygiene (procedural) | literal `check()` names in CHECK_net; no tracked corpus `*.bin`; harness wiring; soft-skip lanes; seed generator | `python3 test/security_hygiene.py` (also at start of `lane_net` / `lane_tar`) |

## Fuzz corpora and release publish

Seed bytes are hex in `test/fuzz_corpus/generate_seeds.py` (do not commit
`*.bin`). Harnesses materialize gitignored working copies under:

| Corpus | Path (generated) | Harness | What it exercises |
| --- | --- | --- | --- |
| DER / Certificate list | `test/fuzz_corpus/tls_der/` | `tls_der_fuzz` | `tls_parse_extensions` / `tls_parse_cert` / certificate-list framing; EKU/SAN/BC/KU value lanes; names_chain + leaf/issuer policy; magic prefixes C1–C9 |
| DER verify + sig | `test/fuzz_corpus/tls_der/` (same) | `tls_verify_fuzz` | `tls_verify_chain` parse/policy/names walker plus production `tls_verify_one` (hosted C montgomery + SHA); WR2→GTS prove in `LLVMFuzzerInitialize`; **not** lane_net smoke (hand / `fuzz_net`) |
| Handshake fragmentation | `test/fuzz_corpus/tls_hs/` | `tls_hs_fuzz` | `tls_handshake_one_append` / `tls_encrypted_flight_append` |

Seed counts are whatever the generator writes; `sh test/fuzz_net --report`
records them.
**lane_net** leaves `MOONWATER_FUZZ_*` unset (20 000 runs / 5 s smoke). Harnesses
print `N seeds, libFuzzer ASan/UBSan (-runs=… -max_total_time=…) clean` on
success, or exit 2 (NOT RUN) when clang cannot link `-fsanitize=fuzzer`.
They do not emit LLVM source-line coverage; `-print_final_stats=0`.

**Deeper local campaign (manual, no CI; does not change lane_net smoke):**

```sh
MOONWATER_FUZZ_SECONDS=120 MOONWATER_FUZZ_RUNS=200000 sh test/fuzz_net --report
# equivalent helper (writes artifacts/fuzz-campaign-report.txt by default):
sh test/fuzz_campaign
```

Budget is deeper than the 20k/5s lane smoke and shorter than the default
one-hour continuous `sh test/fuzz_net`. Reports under `artifacts/` are
gitignored — keep `generate_seeds.py` + this recipe; do not check in `*.bin`
or large campaign dumps. Seed notes: `test/fuzz_corpus/README.md`.

**Release attach (manual, no CI automation):**

1. `sh test/fuzz_net --report` — smoke budget unless `MOONWATER_FUZZ_RUNS` /
   `MOONWATER_FUZZ_SECONDS` already set; longer: e.g.
   `MOONWATER_FUZZ_SECONDS=3600 sh test/fuzz_net --report` (or the deeper
   campaign above / `sh test/fuzz_campaign`).
2. Attach `artifacts/fuzz-report.txt` (JSON: clang version, seed counts,
   budget, per-target duration/exit, `overall_exit`) and
   `test/fuzz_corpus/generate_seeds.py` (or a tarball of materialized
   seeds after `python3 test/fuzz_corpus/generate_seeds.py` — never commit
   those `.bin` files).
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
10. a differential oracle or a documented reason no independent oracle exists.

Passing rows are evidence for a particular build and environment, not a
permanent certification. Unsupported architecture, namespace, sanitizer, or
oracle lanes must be reported as not run rather than silently counted as pass.

## Per-parser length/item/recursion ceilings (net)

Length, item-count, and recursion ceilings already enforced in `src/net/net.c`,
with exact-limit and/or one-over proving checks under `CHECK_net`. There is no
CPU-work budget for these parsers; DNS decompression bounds jumps structurally
(ceiling lowers), and DHCP junk discard is an absolute deadline. The full
mapping (parser → ceiling kind → constant → check name) is the comment ledger
at the top of the `CHECK_net` section in `test/checks.c`.

| Parser | Ceiling | Constant | Hit coverage |
| --- | --- | --- | --- |
| DNS | message length | `DNS_MAX_MESSAGE` | exact + one-over unit; TCP/UDP transport one-over |
| DNS | label / name | 63 / 255 | exact + one-over |
| DNS | CNAME hops | `answers + 1` | chain accept + cycle exhaust |
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
a served root, unknown non-critical extensions, and an inclusive sixty-four
extension ceiling must be accepted. Duplicate extension OIDs (including
unknown and non-adjacent duplicates) and a sixty-five extension work ceiling
are refused even when OpenSSL accepts them; those rows are named deliberate.
Extension policy is exercised at both leaves and intermediates. Deliberately
stricter Moonwater policy is named at the individual case.

HTTP response framing (`--harness http_response_framing`) is the same shape
for the response parser: a written MUST_ACCEPT / MUST_REFUSE matrix (54 cases /
136 checks at tip) held against the in-tree framing and chunk decoder, with
`http.client` as a second oracle. Rows where Moonwater refuses (or accepts a
different body) while `http.client` is looser — TE/CL conflicts, duplicate
framing fields, obs-fold, embedded controls, non-chunked TE, trailer and
chunk-extension grammar, informational framing, leftover after the final
chunk — are named deliberate. Duplicate Date/Host remain accepted (not
framing-critical). HTTPS→HTTP downgrade (`--harness https_downgrade`, 9/9)
exercises wget manners on a TLS loopback for plain and nested `http://`
Locations, sticky secure across hops, uppercase/`//` shapes that stay on
HTTPS, and credentialed/`http:///` Locations that refuse without a silent
fetch. Refresh/meta are not redirect inputs in this tree.
