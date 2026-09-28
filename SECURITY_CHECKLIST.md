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
  (local via `sh test/run fuzz`; defaults one hour / unbounded run count;
  override with `MOONWATER_FUZZ_SECONDS` / `MOONWATER_FUZZ_RUNS`. lane_net
  keeps the short bounded smoke only).
- [x] Run an MSan lane on a hosted build to find uninitialized wire padding
  (local/hosted via `sh test/run msan` or
  `MOONWATER_MSAN=1 python3 test/differential.py --harness msan_net`; also
  `MOONWATER_MSAN=1` on `tls_der_fuzz` / `tls_hs_fuzz`. Scope under
  `-fsanitize=memory`: intentional ABI pad proves (generic wire header and
  a netlink-attr-shaped hole); hosted freestanding lifts of `dns_copy_name`,
  TLS record-header open, HTTP header/chunk framing, `dhcp_walk`,
  `netlink_find_span`, and TLS `tls_parse_extensions`/`tls_parse_cert` (fuzz
  lift without libFuzzer) over fully-initialized hostile buffers with an
  intentional uninit catch per new surface; thin CHECK_net-equivalent
  align/sizeof/parser probes (not a full freestanding `CHECK_net` under
  MSan — that binary needs the moonwater runtime/syscalls); plus short
  DER/HS corpus smoke. Exercised on Lima aarch64 Linux clang; Apple clang
  and many qemu images: NOT RUN. CI remains parked — no push auto-job).
- [x] Record per-parser length, item-count, and recursion ceilings.
  Ledger of those ceilings lives in `test/checks.c` (CHECK_net) and
  `SECURITY_TEST_MATRIX.md` (no CPU-work budgets); exact-limit and one-over
  hit tests cover DNS, TLS, HTTP, and DHCP main parsers.

## Identity and state

- [x] DNS binds replies to ID, exact question, connected peer, and deadline.
- [x] DNS validates every declared answer/authority/additional record and has no
  unaccounted trailing message bytes before classifying success or error rcodes.
- [x] DHCP binds replies to xid, hardware address, server port, and selected
  OFFER peer; its xid uses only initialized CSPRNG output.
- [x] SNTP binds replies to a 64-bit CSPRNG originate nonce and validates mode,
  version, stratum, root metrics, timing, and server agreement.
- [x] Netlink routing messages require kernel port zero and matching sequence.
- [x] DNS identity mutations, a queued prior-xid DHCP reply, and a prior-nonce
  SNTP reply cover replay rejection for every stateful UDP exchange here.
- [x] Exhaustive DHCP renewal/rebinding authorization covers every message
  kind, missing/selected/foreign server, retained/changed address, and phase.
- [x] DHCP option-overload control is unique, primary-only, length one, and 1..3.
- [x] Every primary or overloaded DHCP option stream has an explicit END marker.
- [x] Bytes following DHCP END are canonical zero padding, never hidden options.

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
- [x] SAN registeredID alternatives require complete canonical DER OID arcs.
- [x] Unknown critical and name-constraints extensions fail closed.
- [x] The default v1 certificate version is omitted; explicit v2/v3 remain valid.
- [x] Certificate validity uses canonical UTC/GeneralizedTime at the 2050 pivot.
- [x] KeyUsage has exact DER named-bit encoding, no undefined bits, and enforces
  the decipherOnly/keyAgreement dependency.
- [x] ExtendedKeyUsage purpose OIDs require complete canonical DER arcs.
- [x] Extension envelopes require canonical DER: the sequence is nonempty,
  default-false critical and basic-constraints cA flags are omitted, OIDs are
  canonical, and duplicate OIDs are refused under an explicit count ceiling.
- [x] Basic constraints, path length, key usage, EKU, dates, issuer names, and
  signatures are checked for served chains.
- [x] Generated chain verdicts are compared with OpenSSL; certificate issuance
  selects the modern `x509` date interface or the established `ca` fallback so
  supported host CLI versions cannot silently disable the oracle.
- [x] Bounded lane-smoke coverage-guided fuzzing for DER (5s / 20k runs via
      `python3 test/differential.py --harness tls_der_fuzz`; ASan/UBSan via
      clang libFuzzer when available, else NOT RUN). Lifts also hit EKU/SAN/BC/KU
      value parsers (magic C2–C5), host-aware SAN (C6–C7), ECDSA sig / alg-id
      junk (C8–C9), and pure path policy (`names_chain`, leaf/issuer auth).
      Local continuous: see `sh test/run fuzz` / `MOONWATER_FUZZ_*`.
- [x] Bounded lane-smoke coverage-guided fuzzing for certificate-list framing
      (same `tls_der_fuzz` 5s/20k smoke: seeds plus `tls_certificate_body_open` /
      list walk with `tls_parse_cert` on slices, then the same empty-list /
      leftover refuse as `tls_verify_chain`; expected `TLS_FAIL` ignored). Local
      continuous: see `sh test/run fuzz` / `MOONWATER_FUZZ_*`.
- [x] Bounded lane-smoke coverage-guided fuzzing for handshake fragmentation
      (`python3 test/differential.py --harness tls_hs_fuzz`, 5s / 20k; lifts
      `tls_handshake_one_append` / `tls_encrypted_flight_append` with `tls=null`
      framing; seeds from `tls_fuzz_seeds("tls_hs")`; ASan/UBSan via clang
      libFuzzer when available, else NOT RUN). Local continuous: see
      `sh test/run fuzz` / `MOONWATER_FUZZ_*`.
- [x] Optional verify fuzz (`tls_verify_fuzz`; not lane_net smoke): same
      `tls_der` corpus; mirrors `tls_verify_chain` through parse/policy/names and
      calls production `tls_verify_one` (hosted C montgomery + pure SHA; WR2→GTS
      accept + flipped-sig refuse prove before fuzz). Wired into
      `sh test/run fuzz`; hand-run via
      `python3 test/differential.py --harness tls_verify_fuzz`.

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
  checked pathname between file, directory, and symlink-to-dir forms
  (`python3 test/differential.py --harness pathname_race --binary ours=…`,
  also `sh test/run tar`). Sibling threads cycle contested names
  (`flip`/`parent`/`mid`/`deep`) through regular file ↔ directory ↔
  symlink-to-outside-keep via renameat2/rename while tar extract walks
  leaf/nested/burst/deep member trees; exclusive-create probes cover
  O_EXCL|O_NOFOLLOW leaf, private-edit style dirfd+O_EXCL, shell
  noclobber (`set -C`), and `install -D` through a raced parent.
  Effect-based: outside victim unchanged, keep/ stays empty (no
  nested/deep escape). Lane smoke leaves budget unset (80/2s); longer
  local stress via `MOONWATER_PATHNAME_RACE_ROUNDS` /
  `MOONWATER_PATHNAME_RACE_SECONDS` with the same effect asserts. Honest
  NOT RUN (2) without threads/rename (soft in `lane_tar`).

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
  handshake has no separate mmap (fixed `TLS_HS_MAX` holds —
  `tls_midpath_append_refusal` covers tight-room refuse, not AS). Soft
  `RLIMIT_AS` mmap refuse is proved on the native host arch only (Lima
  aarch64 / Linux arm64 when that lane has no qemu-user); qemu-user arches
  get `--emulated` and log NOT RUN for AS. Once-armed `memory_reserve`
  (shim before `net.c`) proves fail-closed on every arch: first-reserve
  HTTP body / netlink, mid-path capacity
  (`http_body_store_midpath_reserve_fault`), large body next-quantum
  (`http_body_store_large_growth_reserve_fault`), mid-path `net_room`
  (`net_room_midpath_reserve_fault`). Soft-AS mid-path capacity remains
  `http_body_store_midpath_exhaustion` (native only). Also: once-armed
  writev `-ENOSPC` (`http_body_copy_midpath_fault` /
  `http_write_spans_midpath_fault`).
- [x] Descriptor-, mapping-, and allocation-exhaustion sweeps for every
  remaining externally reachable service loop in `src/net` (inventory: DNS
  client only, DHCP client only, rtnetlink request/reply — no DNS/DHCP/HTTP
  servers, no netlink multicast listener). DNS `EMFILE` → `DNS_NO_SERVER`
  (stack messages / TCP frame; no separate map or `byte_store_reserve`);
  DHCP `EMFILE` → `DHCP_NO_SOCKET` (stack packet; no separate map); netlink
  open `EMFILE` and `net_room`/`array_store_reserve` under soft `RLIMIT_AS`
  → failed/empty on native host arch; `--emulated` / missing `prlimit` →
  NOT RUN for AS, with once-armed `memory_reserve` covering first- and
  mid-path `net_room` refuse (`dns_dhcp_netlink_resource_exhaustion` /
  `net_room_midpath_reserve_fault`). Mid-path
  DNS/DHCP send/recv (after the client socket exists) is covered by thin
  `socket_send`/`socket_receive` hooks in `dns_dhcp_midpath_faults`: UDP send
  `ENOSPC`/`EINTR`; UDP recv `EINTR`→`EIO`, `EAGAIN`, short junk→`ENOSPC`,
  sticky `EINTR`×deadline; TCP fallback short-send→`ENOSPC` and
  `EINTR`→`ENOSPC`; DHCP recv same class plus junk-before-fault; DHCP
  reacquire send `ENOSPC` when `BINDTODEVICE lo` works (else honest NOT RUN).
  Open-time `EMFILE` remains the socket-open gate only.
- [x] Publish fuzz corpus coverage and sanitizer versions with each release.
  Run `MOONWATER_FUZZ_REPORT=artifacts/fuzz-report.txt sh test/run fuzz`
  (writes that file and stdout: clang/sanitizer version, seed counts,
  runs/duration/exit per `tls_der_fuzz` / `tls_hs_fuzz` / `tls_verify_fuzz`,
  and the commit whose `tls_fuzz_seeds` in `test/differential.py` made the
  seeds). Attach that report. Defaults match lane_net smoke (20k/5s); set
  `MOONWATER_FUZZ_*` for longer evidence. Exit 2 means libFuzzer unavailable (NOT RUN) — still
  attach the honest report. See `SECURITY_TEST_MATRIX.md`.
