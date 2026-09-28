# Shell and network security model

Moonwater treats every byte from a command line, environment, file, kernel
message, and network peer as hostile until the component which acts on it has
validated it.  Parsing success is not authorization: a message must also be
bound to its transport peer, transaction, protocol state, and resource budget.

## Attacker positions

The testable model distinguishes four attackers:

1. **Local input:** controls shell text, arguments, environment, files, and
   descriptor layout, but has no existing privilege.
2. **Off-path network:** can send forged traffic but cannot observe the live
   exchange. Transaction entropy and exact reply identity must stop it.
3. **On-path or same-link network:** can observe, inject, replay, delay,
   reorder, truncate, and suppress traffic. TLS is expected to authenticate
   HTTPS. Plain DNS, DHCPv4, and SNTP do not authenticate an on-path peer;
   endpoint and nonce checks protect only against off-path forgery.
4. **Hostile service:** owns the connected endpoint and can stream arbitrary
   bytes and timing forever. Bounds and absolute deadlines must contain it.

## Required invariants

- Length arithmetic is checked before addition, subtraction, multiplication,
  alignment, narrowing, allocation, or copy.
- A parser either consumes its complete declared object or fails; no success
  leaves unvalidated trailing framing.
- Every loop over hostile data makes progress and has a byte, item, depth, or
  absolute-time bound.
- Datagram replies match their expected family, peer, transaction, and echoed
  request fields before changing state.
- Security-sensitive transactions use the initialized kernel CSPRNG or do not
  happen. Availability is not an entropy fallback.
- Text is validated again at its serialization or execution sink. Safe callers
  do not make an unsafe sink safe.
- Failures do not publish partial outputs, retain stale authenticated state,
  leak descriptors across exec, or leave secrets live unnecessarily.
- Ambiguous protocol spellings are rejected rather than interpreted according
  to whichever parser happens to run first.

## Protocol review baseline

Reviews use the threat method in RFC 3552, operational questions in RFC 9413,
TLS guidance in RFC 9325, HTTP/1 framing in RFC 9112, DNS forgery guidance in
RFC 5452, DHCPv4 in RFC 2131, NTPv4 in RFC 5905, SEI CERT C, and the relevant
MITRE CWEs. These are review inputs, not claims that conformance alone proves
security.

## Residual risks

- DHCPv4 and unauthenticated SNTP cannot defeat a same-link or on-path peer.
- DNS is not DNSSEC-validating; transaction checks do not authenticate data to
  its zone owner.
- The C implementation cannot obtain compile-time ownership and bounds proofs.
  Guard pages, sanitizers, fuzzing, fault injection, and cross-architecture
  tests compensate but do not turn C into a memory-safe language.
- Resource limits prevent one connection from growing without bound; they do
  not prevent a distributed flood before the kernel or network filters it.

## Finding acceptance

A security fix is complete only when it has a procedural regression which
would fail on the vulnerable implementation, covers the bug family rather
than one sample, and names any residual attacker position it does not stop.

