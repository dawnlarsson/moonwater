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
- [ ] Run network parser fuzz targets continuously under ASan and UBSan.
- [ ] Run an MSan lane on a hosted build to find uninitialized wire padding.
- [ ] Record a per-parser allocation, item-count, recursion, and CPU budget.

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
  DEL, backslash, non-ASCII, and percent-encoded NUL/CR/LF; header values
  reject controls and DEL.
- [x] Userinfo, unsupported schemes, ambiguous authority bytes, and fragments
  are rejected or removed before transmission.
- [x] Duplicate framing fields and TE/CL conflicts are rejected.
- [x] Header names, controls, obsolete folding, chunk lines, extensions, and
  trailers have bounded grammar tests.
- [x] Stream framing is tested at every socket/header split.
- [x] Redirect count and HTTPS downgrade are bounded/refused.
- [ ] Differentially test response framing against multiple independent HTTP
  implementations, retaining a written policy where references disagree.

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
- [ ] Add persistent coverage-guided fuzzing for DER, handshake fragmentation,
  and certificate-list framing.

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
- [ ] Run race tests under a scheduler which continuously exchanges every
  checked pathname between file, directory, and symlink forms.

## Faults, resources, and portability

- [x] Selected entropy policies are tested by seccomp failure injection.
- [x] Partial I/O, EINTR, ENOSPC, deadlines, stream fragmentation, and guard
  boundaries have procedural coverage in existing lanes.
- [x] Network namespace and netem tests exist for privileged network paths.
- [ ] Require x86-64, ARM64, and RISC-V security lanes in CI.
- [ ] Add descriptor-, mapping-, and allocation-exhaustion sweeps to every
  externally reachable service loop.
- [ ] Publish fuzz corpus coverage and sanitizer versions with each release.
