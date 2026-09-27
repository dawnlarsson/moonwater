# Security test matrix

| Threat family | Evidence | Command |
| --- | --- | --- |
| Network byte order, socket boundaries | `socket` lane on each available architecture | `sh test/run socket` |
| Netlink source, sequence, attributes; DNS; HTTP; TLS; DHCP | freestanding network checks | `sh test/run net` |
| UDP replay and DHCP reacquisition authorization | identity mutation, queued prior transaction, exhaustive state cross-product | `sh test/run net machine` |
| Certificate path semantics | generated chains against OpenSSL | `python3 test/differential.py --harness tls_chains` |
| HTTPS→HTTP redirect downgrade | TLS loopback 302 with `http://` Location under wget manners | `python3 test/differential.py --harness https_downgrade` |
| HTTP response framing (chunked, TE/CL, headers, trailers) | written MUST_ACCEPT/MUST_REFUSE matrix; http.client as second oracle with named deliberate disagreements | `python3 test/differential.py --harness http_response_framing` |
| Uninitialized wire / ABI padding (MSan) | intentional padding prove + TLS DER/HS seed smoke under MemorySanitizer; NOT RUN without clang MSan; not CI push | `sh test/msan_net` |
| SNTP nonce, ancillary timestamps, timing arithmetic, server selection | machine checks | `sh test/run machine` |
| Shell parsing, expansion, environment, status and effects | generated Bash/Dash comparison | `sh test/run shell builtins` |
| File traversal, symlink and replacement behavior | effect-based coreutils differential | `sh test/run files` |
| Tar paths and archive framing | tar lane and compression grammar | `sh test/run tar compression` |
| Codec hostile lengths and guard pages | codec-specific and floor lanes | `sh test/run codec compression_floor` |
| Waterlink replay, seal and lossy delivery (separate review scope) | pure transform and namespace integration | `sh test/run waterlink link` |
| Whole available suite | all locally supported lanes | `sh test/run` |
| TLS DER / handshake fuzz (lane smoke) | bounded libFuzzer ASan/UBSan; soft NOT RUN without clang fuzzer | `sh test/run net` (via `tls_der_fuzz` / `tls_hs_fuzz`, 20k/5s) |
| TLS net fuzz continuous (local) | same harnesses; longer budget | `sh test/fuzz_net` (`MOONWATER_FUZZ_*`) |
| Release fuzz attach | machine-readable sanitizer + corpus inventory + run exits | `sh test/fuzz_net --report` → `artifacts/fuzz-report.txt` |

## Fuzz corpora and release publish

Seed corpora live in-tree (attach the tree or a tarball; do not invent coverage):

| Corpus | Path | Harness | What it exercises |
| --- | --- | --- | --- |
| DER / Certificate list | `test/fuzz_corpus/tls_der/` | `tls_der_fuzz` | `tls_parse_extensions` / `tls_parse_cert` / certificate-list framing |
| Handshake fragmentation | `test/fuzz_corpus/tls_hs/` | `tls_hs_fuzz` | `tls_handshake_one_append` / `tls_encrypted_flight_append` |

Seed counts are whatever files are present; `sh test/fuzz_net --report`
records them.
**lane_net** leaves `MOONWATER_FUZZ_*` unset (20 000 runs / 5 s smoke). Harnesses
print `N seeds, libFuzzer ASan/UBSan (-runs=… -max_total_time=…) clean` on
success, or exit 2 (NOT RUN) when clang cannot link `-fsanitize=fuzzer`.
They do not emit LLVM source-line coverage; `-print_final_stats=0`.

**Release attach (manual, no CI automation):**

1. `sh test/fuzz_net --report` — smoke budget unless `MOONWATER_FUZZ_RUNS` /
   `MOONWATER_FUZZ_SECONDS` already set; longer: e.g.
   `MOONWATER_FUZZ_SECONDS=3600 sh test/fuzz_net --report`.
2. Attach `artifacts/fuzz-report.txt` (JSON: clang version, seed counts,
   budget, per-target duration/exit, `overall_exit`) and
   `test/fuzz_corpus/` (or `tar czf fuzz_corpus.tgz test/fuzz_corpus`).
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

## Per-parser resource budgets (net)

Length and item-count ceilings already enforced in `src/net/net.c`, with
exact-limit and/or one-over proving checks under `CHECK_net`. There is no
separate CPU-work counter for these parsers; DNS decompression bounds jumps
structurally (ceiling lowers), and DHCP junk discard is an absolute deadline.
The full mapping (parser → budget kind → constant → check name) is the comment
ledger at the top of the `CHECK_net` section in `test/checks.c`.

| Parser | Budget | Constant | Hit coverage |
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
