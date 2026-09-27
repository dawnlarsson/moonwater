# Shell and network audit checklist

Use this list for every security review. `[x]` means there is current code and
procedural coverage; `[ ]` is work still required, not an assertion of a bug.

## Bytes, memory, and arithmetic

- [x] Bounded cursor parsers for netlink, DNS, HTTP, TLS records, and DHCP.
- [x] Truncated and oversized datagrams are distinguished with `MSG_TRUNC`.
- [x] Network integer loads do not assume alignment.
- [x] Wire-field narrowing is checked for netlink attributes and messages.
- [x] HTTP lengths and chunk sizes reject native-word overflow.
- [x] Guard-page coverage exists for shared bounded primitives and codecs.
- [x] Run network parser fuzz targets continuously under ASan and UBSan
  (local via `sh test/fuzz_net`; defaults one hour / unbounded run count;
  override with `MOONWATER_FUZZ_SECONDS` / `MOONWATER_FUZZ_RUNS`. lane_net
  keeps the short bounded smoke only).
- [x] Run an MSan lane on a hosted build to find uninitialized wire padding
  (local/hosted via `sh test/msan_net` or
  `python3 test/differential.py --harness msan_net`; also
  `MOONWATER_MSAN=1` on `tls_der_fuzz` / `tls_hs_fuzz`. Scope under
  `-fsanitize=memory`: intentional ABI pad proves (generic wire header and
  a netlink-attr-shaped hole), thin hosted lifts of `dns_copy_name` and TLS
  record-header open over fully-initialized hostile buffers, plus short
  DER/HS corpus smoke. Not a full `CHECK_net` under MSan. Exercised on Lima
  aarch64 Linux clang; Apple clang and many qemu images: NOT RUN. CI remains
  parked — no push auto-job).
- [x] Record per-parser length, item-count, and recursion ceilings.
  Ledger of those ceilings lives in `test/checks.c` (CHECK_net) and
  `SECURITY_TEST_MATRIX.md` (no CPU-work budgets); exact-limit and one-over
  hit tests cover DNS, TLS, HTTP, and DHCP main parsers.

## Identity and state

- [x] DNS binds replies to ID, exact question, connected peer, and deadline.
- [x] DHCP binds replies to xid, hardware address, server port, and selected
  OFFER peer; its xid uses only initialized CSPRNG output.
- [x] SNTP binds replies to a 64-bit CSPRNG originate nonce and validates mode,
  version, stratum, root metrics, timing, and server agreement.
- [x] Netlink routing messages require kernel port zero and matching sequence.
- [x] DNS identity mutations, a queued prior-xid DHCP reply, and a prior-nonce
  SNTP reply cover replay rejection for every stateful UDP exchange here.
- [x] Exhaustive DHCP renewal/rebinding authorization covers every message
  kind, missing/selected/foreign server, retained/changed address, and phase.

## HTTP and URL handling

- [x] Request components are validated at the wire serializer.
- [x] Host has one allowlisted byte grammar; target rejects controls, space,
  DEL, backslash, non-ASCII, and percent-encoded NUL/CR/LF including nested
  `%25` peelings; header values reject controls and DEL.
- [x] Userinfo, unsupported schemes, ambiguous authority bytes, and fragments
  are rejected or removed before transmission.
- [x] Duplicate framing fields and TE/CL conflicts are rejected.
- [x] Header names, controls, obsolete folding, chunk lines, extensions, and
  trailers have bounded grammar tests.
- [x] Chunked body framing is tested across header/socket splits, plus header
  deadline trickle.
- [x] Redirect count and HTTPS downgrade are bounded/refused
  (`python3 test/differential.py --harness https_downgrade` for e2e
  HTTPS→HTTP Location refusal under wget manners).
- [x] Differentially test response framing via a written matrix and
  `http.client` as a second oracle with deliberate disagreements
  (`python3 test/differential.py --harness http_response_framing`).

## TLS and certificates

- [x] Transcript, Finished, record sequence, AEAD tag, and close handling have
  pure or loopback checks.
- [x] SAN matching distinguishes DNS and IPv4 names and constrains wildcards.
- [x] Unknown critical and name-constraints extensions fail closed.
- [x] Duplicate extension OIDs, including unknown ones, are refused under an
  explicit per-certificate extension-count/work ceiling.
- [x] Basic constraints, path length, key usage, EKU, dates, issuer names, and
  signatures are checked for served chains.
- [x] Generated chain verdicts are compared with OpenSSL; certificate issuance
  selects the modern `x509` date interface or the established `ca` fallback so
  supported host CLI versions cannot silently disable the oracle.
- [x] Bounded lane-smoke coverage-guided fuzzing for DER (5s / 20k runs via
      `python3 test/differential.py --harness tls_der_fuzz`; ASan/UBSan via
      clang libFuzzer when available, else NOT RUN). Local continuous: see
      `test/fuzz_net` / `MOONWATER_FUZZ_*`.
- [x] Bounded lane-smoke coverage-guided fuzzing for certificate-list framing
      (same `tls_der_fuzz` 5s/20k smoke: seeds plus `tls_certificate_body_open` /
      list walk with `tls_parse_cert` on slices, then the same empty-list /
      leftover refuse as `tls_verify_chain`; expected `TLS_FAIL` ignored). Local
      continuous: see `test/fuzz_net` / `MOONWATER_FUZZ_*`.
- [x] Bounded lane-smoke coverage-guided fuzzing for handshake fragmentation
      (`python3 test/differential.py --harness tls_hs_fuzz`, 5s / 20k; lifts
      `tls_handshake_one_append` / `tls_encrypted_flight_append` with `tls=null`
      framing; seeds under `test/fuzz_corpus/tls_hs/`; ASan/UBSan via clang
      libFuzzer when available, else NOT RUN). Local continuous: see
      `test/fuzz_net` / `MOONWATER_FUZZ_*`.

## Shell and operating-system boundary

- [x] Shell language and builtins have generated differential tests against
  Bash and Dash.
- [x] Private edit files use random private directories, exclusive creation,
  descriptor-relative reads, and cleanup.
- [x] PTY setup checks descriptor installation and handles standard-descriptor
  reuse.
- [x] Tar extraction pins parent directories and refuses traversal/symlink
  replacement races.
- [x] Generated hostile-environment and privileged-shell matrices cover
  `PATH`, `IFS`, `PS4`, `ENV`, `BASH_ENV`, exported functions, option imports,
  mismatched real/effective/saved IDs, and closed standard descriptors.
- [x] Run race tests under a scheduler which continuously exchanges every
  checked pathname between file, directory, and symlink forms
  (`python3 test/differential.py --harness pathname_race --binary ours=…`,
  also `sh test/run tar`). Sibling thread cycles contested names through
  regular file ↔ directory ↔ symlink-to-outside via renameat2/rename while
  tar extract (and O_EXCL|O_NOFOLLOW create) run; effect-based: outside
  victim unchanged, no nested escape. Honest NOT RUN (2) without
  threads/rename.

## Faults, resources, and portability

- [x] Selected entropy policies are tested by seccomp failure injection when
  seccomp is available; else NOT RUN (e.g. under qemu-user).
- [x] Partial I/O, EINTR, ENOSPC, deadlines, stream fragmentation, and guard
  boundaries have procedural coverage in existing lanes.
- [x] Network namespace and netem tests exist for privileged network paths.
- [x] Gate for x86-64, ARM64, and RISC-V security lanes exists in the parked
  `security` workflow job (workflow_dispatch only; not auto on push).
- [x] Descriptor- and mmap/`byte_store_reserve`-exhaustion sweeps for the
  HTTP fetch / TLS client open loops (`EMFILE` → `HTTP_NO_ROUTE`; reserve
  failure → `HTTP_NO_REPLY`). Accept N/A (client connect only); TLS
  handshake has no separate mmap. Mid-path additions (not open-time only):
  connected-stream body store at capacity then soft `RLIMIT_AS` on the next
  `byte_store_reserve` (`http_body_store_midpath_exhaustion`; honest NOT RUN
  when guest AS limits are ignored); connected-stream body copy with
  once-armed writev `-ENOSPC` (`http_body_copy_midpath_fault`); writev short
  prefix then once-armed `-ENOSPC` (`http_write_spans_midpath_fault`); TLS
  plaintext/encrypted-flight append past a tight hold room after a retained
  prefix (`tls_midpath_append_refusal`).
- [x] Descriptor-, mapping-, and allocation-exhaustion sweeps for every
  remaining externally reachable service loop in `src/net` (inventory: DNS
  client only, DHCP client only, rtnetlink request/reply — no DNS/DHCP/HTTP
  servers, no netlink multicast listener). DNS `EMFILE` → `DNS_NO_SERVER`
  (stack messages; no separate map); DHCP `EMFILE` → `DHCP_NO_SOCKET`
  (stack packet; no separate map); netlink open `EMFILE` and
  `net_room`/`array_store_reserve` under soft `RLIMIT_AS` → failed/empty.
  Honest NOT RUN when `prlimit` is unavailable or address-space limits are
  ignored under emulation (`dns_dhcp_netlink_resource_exhaustion`). DNS/DHCP
  mid-path send/recv fault injection remains open-time-only at the socket
  open gate (no new mid-path DNS proofs).
- [x] Publish fuzz corpus coverage and sanitizer versions with each release.
  Run `sh test/fuzz_net --report` (writes `artifacts/fuzz-report.txt` and
  stdout: clang/sanitizer version, seed counts, runs/duration/exit per
  `tls_der_fuzz` / `tls_hs_fuzz`). Attach that report plus
  `test/fuzz_corpus/generate_seeds.py` (hex source of truth; `*.bin` is
  gitignored and materialized at run time) or a tarball of generated seeds.
  Defaults match lane_net smoke (20k/5s); set `MOONWATER_FUZZ_*` for longer
  evidence. Exit 2 means libFuzzer unavailable (NOT RUN) — still attach the
  honest report. See `SECURITY_TEST_MATRIX.md`.
