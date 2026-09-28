# Network security review status

This is a gap register, not a claim that the network stack is “bulletproof.”
It exists to stop security work becoming a loop of adding another isolated
malformed packet. A pass counts as new evidence only when it closes a named
threat family, demonstrates that the test fails without the fix, or removes a
gap below.

## Current assessment

The stack has strong hand-written boundary checks and unusually broad
deterministic regression coverage. It is **not yet release-grade evidence for
memory safety or parser completeness** because the continuous sanitizer,
coverage-guided fuzzing, cross-architecture, and independent HTTP oracle lanes
listed below do not exist or are not mandatory.

| Area | Evidence present | Important remaining gap |
| --- | --- | --- |
| Netlink | sender PID, sequence, length/alignment, multipart and truncation checks | persistent fuzzing of nested attributes; mandatory namespace runs |
| DNS | exact question/ID binding, compression loops, full RR framing, UDP truncation to TCP | coverage-guided compression/name fuzzing; independent packet oracle; DNSSEC is out of scope |
| TLS records/handshake | record and handshake fragmentation, transcript/Finished, AEAD limits, state ordering | persistent record/handshake fuzzing; allocation and I/O fault sweeps |
| X.509 | strict DER and generated-chain policy matrix against OpenSSL | a second independent path validator; published corpus coverage; name-constraints breadth |
| HTTP/URL | sink-side request validation, framing conflicts, split-point and chunk/trailer checks | multiple response-framing oracles; slow-stream scheduling tests; broader redirect URL corpus |
| DHCPv4 | peer/xid/MAC binding, option framing/overload, state cross-product, entropy faults | coverage-guided option-stream fuzzing; mandatory namespace/netem retransmission runs |
| SNTP | nonce and peer binding, ancillary timestamp parsing, arithmetic and selection checks | adversarial scheduling/netem as a mandatory lane; era-boundary integration tests |

The unauthenticated-protocol limits in `SECURITY.md` remain fundamental: an
on-path attacker can forge DHCPv4, ordinary DNS, and SNTP. Parser hardening
does not convert those protocols into authenticated ones.

## Priority queue

### P0 — evidence needed before a high-assurance claim

1. Build persistent fuzz targets for DNS names/RRs, TLS records and certificate
   lists/DER, HTTP response framing/chunks, DHCP option streams, and SNTP
   control messages. Seed them from the unit corpus and run ASan+UBSan.
2. Make x86-64 ASan+UBSan and native namespace/netem runs required CI jobs;
   publish compiler and sanitizer versions, corpus hashes, executions, edges,
   crashes, and time budget rather than merely saying “fuzzed.”
3. Run the hosted parsers under MSan. Keep this separate from UBSan: it answers
   the different question of whether wire output or decisions consume
   uninitialized bytes.
4. Differentially exercise HTTP response framing against at least two mature
   implementations and pin Moonwater's policy for every disagreement.

### P1 — resource and state-machine assurance

1. Record and test a byte, item, allocation, recursion, descriptor, and CPU
   budget for every externally reachable parser or transaction loop.
2. Inject allocation, `mmap`, descriptor, short-I/O, `EINTR`, clock-jump, and
   deadline failures at every relevant call site, checking cleanup and that no
   partially authenticated state is published.
3. Make ARM64 and RISC-V network lanes required, including sanitizer-capable
   hosted builds where available. “Not run” is not evidence.
4. Add deterministic hostile scheduling for TCP/TLS/HTTP: one-byte delivery,
   long pauses on every boundary, early FIN/RST, simultaneous timeout, and
   response bytes arriving with close.

### P2 — independent and operational assurance

1. Add a second X.509 path-building oracle and a durable corpus of real and
   synthetic certificates, including name constraints and unusual but valid
   chains.
2. Run namespace tests with loss, duplication, reordering, delay, MTU changes,
   stale queued datagrams, and source-address changes at every protocol phase.
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

Until the P0 queue is complete, the honest rating is **strong deterministic
unit/integration coverage, incomplete high-assurance evidence**. More isolated
unit cases may still fix real bugs, but they do not move that rating by
themselves.
