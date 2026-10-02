# Network attack campaign IV: semantic seams and hostile ecology

This is the plan for the pass after pull requests 12, 16 and 24. Campaign III
already owns packet scheduling, stale queues, network epochs, resource
amplification, Wi-Fi and Waterlink lifecycle, persistence faults, kernel ABI
lengths and independent parser oracles. This campaign does not rename those
items and claim novelty. It attacks the seams still outside that plan: two
layers assigning different identities to the same bytes, authority inherited
from the local environment, randomness repeated across lifecycle boundaries,
individually bounded mechanisms forming an unbounded feedback loop, secrets
escaping without a parser failure, and hardware or kernel transformations that
make the program observe a different packet from the one a test generated.

Nothing below is a claim that Moonwater is vulnerable. Each bullet is a
hypothesis to disprove in an isolated namespace, disposable guest or hwsim
radio. No spoofing, flood, reflection or credential attack belongs on a public
network.

## The bar for crossing out a hypothesis

For every family, record all of the following:

1. the least-privileged attacker, exact asset, trust boundary and one-sentence
   invariant;
2. the generated event transcript, seed, kernel and architecture, plus packet
   capture on both sides of every kernel transform;
3. a parent-tree or single-fault mutant that goes red, and adjacent legal cases
   that remain green;
4. maximum elapsed monotonic time, packets, bytes, allocations, resident set,
   descriptors, child processes, persistent writes and cryptographic work;
5. an honest transaction running concurrently, with a recovery bound after
   hostile input stops;
6. the layer that actually enforced the property (Moonwater, kernel, driver,
   filesystem, peer or test fixture), and residual attacker capability.

A refusal is not a pass when it leaks a secret, mutates durable state, spends
unbounded work, emits a larger attack, or starves the honest transaction.
Likewise, ASan silence, protocol conformance and agreement with one peer are
supporting evidence, not closure.

## I. One byte string, several security identities (P0)

Attack every conversion where DNS, URL, HTTP, TLS, socket and policy code can
name the same destination differently.

- Generate hosts as DNS wire labels, presentation names, URL authorities,
  Host fields, SNI values, certificate SANs and socket addresses. Cross trailing
  dots, case, empty/interior labels, maximal labels, embedded NUL in lengthful
  forms, percent escapes, repeated decoding, userinfo, brackets, zone IDs,
  decimal/hex/octal-looking IPv4, IPv4-mapped IPv6 and Unicode confusables.
- Make redirects change only representation while retaining an address, then
  retain spelling while DNS changes every address. Cross CNAME aliases with SAN
  wildcards and public-suffix boundaries. Fail each connect candidate so policy
  must be re-applied to the next one.
- Assert a single immutable tuple `{scheme, canonical host, port, resolved
  address, interface/scope, trust tier}` is what redirect policy, SNI, Host,
  certificate matching and the eventual `connect` all authorize. No layer may
  validate one tuple and use another.
- Test DNS-over-TLS bootstrap and NTS server redirection specially: the name
  used to establish authenticated transport must not be replaced by an answer
  learned through that transport before authorization.

**Evidence:** build a cross-layer identity harness, not six unit tests. It
records each representation at each sink and compares the final connected
4/6-tuple with the tuple authorized by policy. Differential peers include a
browser-style URL parser, OpenSSL hostname checking, and the kernel's
`inet_pton`; disagreements are named rather than majority-voted.

**First finding.** The initial URL accepted `https:443` as the schemeless
authority `https` on port 443 and then used plaintext, although an RFC 3986
parser treats the leading `https:` as a scheme. The parser now reserves the
`http:` and `https:` spellings even without `//`, refusing this ambiguity while
retaining an unambiguous schemeless `h:81`. The URL matrix kills removal of the
check, including mixed case. This closes that spelling, not the cross-layer
identity family: redirects, DNS answers, certificate names and the connected
tuple still need the combined harness below.

**Second finding.** Waterlink's legacy-unicast mDNS responder charged its
global reply interval before `sendto`. A same-link raw sender could continuously
forge an otherwise valid question with UDP source port zero: Linux refused
each reply, but each failed send still moved the global timestamp and suppressed
honest one-shot discovery replies. Port zero is now neither a legacy-unicast
nor multicast question, the timestamp advances only after the complete reply
is accepted by the kernel, and the socket regression puts the forged
destination immediately before an honest asker. Successful
spoofed reflection remains bounded by the existing global reply interval; this
does not close the broader amplification campaign.

**Third finding.** The mDNS reader treated a PTR/ANY question for the service
as answerable without checking its DNS class, and consumed SRV ports from the
same wrong-class records. That made Moonwater a legacy-unicast response oracle
for CHAOS and arbitrary classes that conforming mDNS peers ignore, widening the
reflection grammar and letting semantically unrelated records drive greeting
work. Questions and SRV records now require Internet class while retaining the
mDNS QU/cache-flush high bit. The parser regression covers both the refused
CHAOS question and a valid QU question; the whole reader remains under its
ASan/UBSan fuzz target.

**Fourth finding.** Two mDNS fields which peers use to discard traffic still
reached work here. A query carrying a nonzero response code was accepted even
though RCODE is zero in a query, so malformed packets ignored by compliant
responders could reach Moonwater's response budget. More importantly, an SRV
with TTL zero—the DNS-SD goodbye a cache removes—was rediscovered as a live
endpoint and launched a cryptographic greeting. Queries now require zero
opcode and RCODE in either direction, and a zero-TTL SRV cannot create an
instance. Regressions put both shapes through the complete mDNS reader. This
does not authenticate discovery; it makes discard semantics agree before work.

**Fifth finding.** SRV RDATA was treated as “six bytes, then anything”: the
reader took priority, weight and port but never parsed the target name or
required it to consume the RDATA. It consequently greeted port zero, the root
target which means a service is unavailable, and malformed targets with
trailing bytes—shapes other DNS-SD peers do not turn into endpoints. The target
is now parsed by the bounded DNS name walker, must be non-root and end exactly
with the record, and the port must be nonzero before an instance exists. The
production announcement is mutated into each rejected shape, and the service
test proves a packet full of port-zero announcements spends neither entropy nor
the global greeting budget. Unreachable nonzero ports remain covered by that
budget and by the broader discovery-amplification campaign.

**Sixth finding.** An SRV owner that merely ended in
`_waterlink._udp.local` was treated as a discovered instance even when no PTR
under the service advertised it. That let an orphan record which DNS-SD
browsers never surface launch the same greeting work as a complete service
announcement. The reader now records the bounded PTR and SRV halves in either
wire order and exposes only their intersection after the whole packet is
walked. PTR RDATA itself must be one exact, non-goodbye Internet-class instance
name. A mutation changes the production announcement's PTR into another record
while leaving its SRV intact; the old reader finds it and the new reader does
not. This binds discovery work to DNS-SD's actual service-to-instance relation,
not just a suffix that attacker bytes can copy.

**Seventh finding.** The global ring bounded forged-announcement greetings to
sixteen per ten seconds, but one ordinary source could spend all sixteen with
different instance/port pairs and leave no slot for an honest new peer. The
ring now also gives each unauthenticated source at most eight recent greetings:
enough to greet one real machine in every supported group, while reserving
half the global capacity for another address. The regression fills the source
with distinct valid production announcements and proves it stops at the private
ceiling rather than the global one. A raw sender rotating spoofed addresses can
still reach the global ceiling; cookie or reachability proof before discovery
crypto remains campaign work. Both the exact-place and source-window age checks
use saturating `link_age`, so a backward injected clock does not turn subtraction
wrap into a fresh budget.

**Eighth finding.** HTTP body progress had only a renewable 30-second idle
timer. A hostile origin could therefore send one byte before every expiry and
hold a fetch, its descriptor, and an output file forever without violating any
individual read limit. Every tier now starts one five-minute body deadline and
passes its remaining time through plaintext and TLS reads; parser compatibility
does not grant unbounded resource lifetime. A socket regression delivers legitimate
payload bytes every 25 ms but injects a 100 ms whole-body budget, proving that
continuous progress cannot renew it. Output growth remains bounded by time,
not bytes, when streaming to disk; a caller-owned size policy is still needed
where disk consumption itself is the security boundary.

**Ninth finding.** Reserving bare `http:` and `https:` closed only two members
of a general URI ambiguity. The initial URL parser still accepted `ftp:21` or
`smtp:25` as a schemeless HTTP host and port, while the redirect resolver and
an RFC URI parser classified those same bytes as unsupported schemes. A policy
or wrapper using URI semantics could therefore authorize one identity while
the transport used another. The tight tier now treats every leading RFC 3986
scheme token as a scheme and requires `//` authority syntax; consequently its
hostname-with-port form is `http://h:81`, not `h:81`. The default tier retains
the historical shorthand, except for the already-reserved web schemes, for
wget compatibility. Unit rows cross multiple non-web schemes and the shortest
one-letter host, and the HTTP fuzz corpus carries the ambiguity through both
tiers.

**Tenth finding.** The repaired legacy-unicast mDNS path charged its reply
interval only after a complete send, but the multicast-answer path still moved
`last_answer` before attempting any send. A transient kernel refusal therefore
let an attacker consume the shared answer interval without causing an answer,
suppressing the next honest query. Its raw unsigned age calculation also made
a backward injected monotonic sample look almost 2^64 microseconds old and
bypass the limiter. Multicast send helpers now report whether any interface
accepted the complete datagram; only that success advances the budget, and the
age uses the shared saturating calculation. The regression drives both a
deterministic `EBADF` send and a timestamp older than the saved answer. This
closes accounting parity between the two reply modes, not spoofed-query
amplification after a successful send.

**Eleventh finding.** Migration of legacy Waterlink group records erased a
fast password-verifier field with one unchecked `pwrite64` per record and an
unchecked `fsync`. `EINTR`, a short write, `ENOSPC`, or a durability failure
could therefore leave all or part of the offline guessing oracle on disk while
the listener proceeded as though migration had succeeded. The positional
writer now retries interruption, completes short writes without shifting the
next record, rejects impossible counts, and requires `fsync`; failure scrubs
the in-memory groups and refuses to activate them so a later load retries the
migration. The regression uses `/dev/full` as a deterministic refused
positional write, while the existing record test proves that successful
migration preserves the slow key and grants but zeroes the verifier. Atomic
replacement remains inappropriate here without a groups-file lock because it
could overwrite a concurrent join; the descriptor-bound in-place migration
retains its inode/type/owner/mode checks.

**Twelfth finding.** Waterlink selected an outgoing mDNS interface with
`IP_MULTICAST_IF` but ignored that operation's result and sent regardless.
Linux retains the socket's previous multicast-interface choice after a failed
`setsockopt`, so an interface disappearing or an invalid/stale index could put
the next interface's announcement—carrying its address—onto the prior link.
The selection is now part of send success: refusal stops before `sendto`, and
therefore cannot count as a successful announcement either. The socket
regression first selects loopback and sends, then supplies an impossible
ifindex; the old code sends through retained loopback while the repaired code
does not send. This binds the advertised interface identity to the kernel
egress selection instead of assuming a failed state transition took effect.

**Thirteenth finding.** Waterlink's service already used saturating elapsed
time, but its reliable-link core still subtracted timestamps directly in the
loss and delayed-ack paths. A receive batch may install state using a newer
clock sample than the batch snapshot passed into the link, and virtualization
or injected monotonic faults can also move backward. Unsigned subtraction then
looked almost 2^64 microseconds old: a frame from the apparent future was
declared lost and an acknowledgement not yet owed became immediately due. The
core now requires `now >= then` before every elapsed comparison, while nearby
reload/interface pacing uses the shared saturating age. Deterministic rows put
both a flight timestamp and an owed-ack timestamp one second ahead and require
no loss, timeout, or acknowledgement. This removes attacker-visible recovery
and congestion transitions caused solely by clock ordering.

**Fourteenth finding (evidence failure).** The hosted mDNS sanitizer model had
drifted behind the production PTR/SRV-intersection rule: it generated orphan
SRV records with random, usually malformed target bytes and still expected
them to surface. The lane consequently reported tens of thousands of model
failures and exercised zero discovered instances, leaving the hardened success
path effectively uncovered even though malformed-input ASan coverage ran. Its
generator now emits exact service PTR and valid non-root SRV pairs in both wire
orders, intersperses unrelated TXT records, repeats labels so last-port and
deduplication behavior are checked, and overflows the bounded instance table.
The deterministic 150,000-case run now observes hundreds of thousands of live
instances with zero model or round-trip failures. A red security lane is an
open security gap; it is never evidence merely because the failure is in its
oracle rather than production.

## II. Ambient authority and local confused deputies (P0)

Network input is not the only hostile input to a network client running with
the guest's authority.

- Start each command with hostile current directory, umask, locale, timezone,
  environment, inherited descriptors, signal mask/dispositions, controlling
  terminal, resource limits and pre-existing children. Pass descriptors at 0,
  1, 2 and the numbers normally allocated to sockets; make them pipes, eventfds,
  packet sockets and directories.
- Race local processes to bind client and service ports, replace Unix/FIFO state
  endpoints, consume predictable temporary names, hold advisory locks and
  mutate configuration between validation and use. Repeat across privilege
  changes, fork/exec and daemon restart.
- Deliver signals after every successful syscall and before its state commit.
  Exercise short successful reads/writes as well as `EINTR`, `EAGAIN`, EOF and
  cancellation. A retry must neither duplicate a network action nor publish a
  half-applied local action.
- Treat logs, status output and terminal control sequences as sinks. Hostnames,
  SSIDs, certificate names, DHCP strings, Waterlink names and HTTP errors must
  not forge log records, terminal actions or shell input.

**Invariant:** unauthenticated bytes and an unprivileged local process cannot
make the privileged network machinery open, overwrite, signal, execute or send
through an object it did not deliberately authorize.

**Evidence:** a syscall-boundary scheduler sweeps failure and replacement after
every open/bind/connect/accept/rename/fork/exec transition. The transcript lists
descriptor provenance and filesystem `(mount,id,inode,type)` identity, not just
path strings.

## III. Entropy continuity, clone and rollback (P0)

Entropy tests currently prove individual acquisition failures; attack uniqueness
across machine lifecycles.

- Snapshot a guest immediately before and after random acquisition, clone it,
  restore it repeatedly, fork at each point and roll the persistent disk back
  while retaining or replacing runtime state. Start with an empty, blocked,
  short-reading and repeated random source.
- Compare DNS IDs/case masks/source ports, DHCP transaction IDs, TLS key shares
  and client randomness, SAE scalars, Waterlink ephemeral keys/cookies/nonces,
  NTS unique identifiers and temporary file names across every clone.
- Force counters to their maximum, restore an old counter with a current key,
  restore an old key with a current counter, and run two cloned peers with the
  same durable identity. Include suspend/resume and process crash between
  reservation and durable commit.
- Do not “fix” repetition by mixing wall time, PID, addresses or other public
  values into weak randomness. On failure, refuse the security operation before
  emitting a reusable authenticator or key-dependent packet.

**Evidence:** a clone harness boots paired snapshots and fails on any repeated
value whose security argument requires uniqueness. A deterministic random-source
shim makes collision tests reproducible; a separate real-entropy run verifies
that the shim did not become the production path.

## IV. Bounded parts, unbounded control loops (P0)

Find cycles spanning protocols and supervisors. Per-parser limits do not bound a
machine that endlessly restarts the parser.

- Construct DHCP NAK/acquire, link flap, duplicate-address, route loss, DNS
  fallback, EDNS fallback, TCP fallback, TLS retry, redirect, NTS re-key,
  Waterlink rekey and Wi-Fi reassociation cycles. Search combinations, not only
  repetitions of one event.
- Make success occur just late enough to reset backoff, then fail the next
  layer. Alternate two attacker identities so each private limiter cools while
  the other acts; rotate IPv6 addresses, ports, BSSIDs and authenticated peer
  identities under the shared global budget.
- Trigger positive feedback: error reply causes retry, retry causes state write,
  state write wakes watcher, watcher tears down a different protocol, teardown
  causes discovery. Include log growth, child churn and flash/disk wear as
  resources.
- Verify exponential backoff has jitter without overflow, a global ceiling, no
  attacker-controlled reset before useful progress, and a quiet-period recovery
  bound. Essential local control and one established honest flow must remain
  usable.

**Evidence:** extract a transition graph from trace events and have a bounded
model checker search for strongly connected components with attacker-controlled
edges and no decreasing budget. Replay every reported cycle in a namespace for
a fixed virtual hour and enforce per-hour packet, process, write and CPU caps.

## V. Cross-tenant and cross-interface isolation (P0)

Campaign III checks stale epochs; this track asks whether two live principals
can consume or receive one another's authority.

- Run simultaneous DHCP, DNS, NTS, HTTP and Waterlink operations on two
  interfaces, network namespaces and groups with deliberately colliding IDs,
  names, addresses and timing. Add VRFs/policy routes where supported.
- Reuse a source port and file descriptor immediately after close; deliver late
  packets, asynchronous errors and readiness notifications to the replacement.
  Exercise `dup`, fork inheritance and descriptor-number wrap.
- Bind IPv6 link-local peers with identical addresses on different scopes;
  overlap RFC1918 addresses on two interfaces; use an IPv4-mapped address and a
  NAT rebinding that preserves some but not all of the tuple.
- Confirm caches, rate limits, replay windows, cookies, saved leases, resolver
  state and authenticated sessions include the full principal/namespace/
  interface epoch needed for their authority, but cannot be bypassed merely by
  changing an irrelevant spelling.

**Invariant:** activity authorized for principal A can neither mutate B nor
charge B's private budget, and a global defensive budget still caps aggregate
hostile work.

## VI. Secret egress and microarchitectural oracles (P1)

- Inventory all secret-bearing buffers and copies: passwords, PMKs, SAE state,
  TLS/Noise/NTS traffic keys, cookies, OpenPGP verification intermediates and
  downloaded credentials. Inspect logs, crash reports, core dumps, argv/env,
  freed/reallocated memory, swap and persistent `.new` files after every failure
  point.
- Measure instruction counts, branches, cache sets and wall time for valid and
  invalid secrets at every early/late rejection site. Include table-index and
  page-fault traces, not only source review or total instruction equality.
- Attack response and timing oracles remotely with randomized network noise and
  enough samples to bound distinguishability. Separately test same-host cache
  observation, SMT contention and scheduler preemption; label which attacker
  position each result supports.
- Verify compiler/LTO and every architecture actually preserve zeroization and
  constant-time claims. Test signal/core-dump interruption between last use and
  wipe.

**Evidence:** produce a secret-lifetime map and Welch t-test/dudect-style report
with predeclared thresholds. A statistically quiet hosted lift is not evidence
for a different optimized freestanding binary or physical CPU.

## VII. Kernel, NIC and wire disagreement (P1)

Campaign III covers ABI lengths and asynchronous errors. This track covers
semantic rewriting below the syscall boundary.

- Toggle GRO/GSO/TSO, checksum offload, VLAN acceleration, RX hashing, packet
  coalescing and timestamping. Compare captures at the generated, veth/tap,
  host and guest boundaries; poison padding and checksum fields before send.
- Exercise UDP checksum zero/invalid rules for IPv4 and IPv6, tiny/giant
  segments, ECN, DSCP, urgent data, half-close, simultaneous close, RST versus
  unread data, SYN/data combinations and port reuse after TIME_WAIT. Attribute
  kernel-owned TCP behavior explicitly.
- Compose IPv4 options, IPv6 extension chains, atomic fragments, overlapping
  fragments, VLAN/QinQ and PMTU/ICMP errors with namespace routing and firewall
  rules. Ensure policy sees the post-reassembly address and interface actually
  used.
- Repeat on at least two kernel versions and two physical NIC/driver families.
  hwsim, veth and loopback results do not prove firmware or offload behavior.

**Evidence:** a wire-equivalence harness hashes normalized packets at each
observation point and flags any transform not in an allowlist with an owner and
security consequence.

## VIII. Consensus, downgrade and split-world peers (P1)

- Put Moonwater, its resolver, proxy/middlebox and origin on deliberately
  different interpretations of HTTP framing, DNS aliases, TLS alerts and
  certificate paths. A stricter endpoint is not enough if an intermediary
  forwards a different request or caches a different response.
- Serve different but individually valid DNS, certificate, redirect, NTS and
  signed-artifact views by address family, interface, retry number and range
  request. Recombine metadata from one view with bytes from another.
- Force every optional upgrade to fail after partial success: EDNS, DNS-over-
  TLS, TCP DNS, NTS, PMF/WPA3, TLS key-share selection and Waterlink cookie
  proof. Record whether fallback is permitted, which security facts survive,
  and whether an attacker can pin the system to the weaker mode.
- Test version skew during rolling replacement: old saved state/new binary,
  new state/old binary, and peers on adjacent protocol versions. Unknown
  security-critical fields must not become silently ignored authority.

## IX. Recovery is part of the security property (P1)

- Remove hostile traffic after each campaign and require convergence without a
  reboot, manual file deletion, wall-clock repair or waiting for attacker-chosen
  expiry. Then repeat with a crash at peak pressure.
- Check that temporary bans, avoid lists, DHCP leases, DNS choices, PMTU,
  replay windows, NTS cookies, Waterlink buckets and Wi-Fi state neither persist
  attacker damage forever nor forget authenticated revocation too early.
- Run disk-full, memory-pressure and descriptor-pressure cleanup while an honest
  transaction proceeds. Cleanup must be idempotent and must not delete another
  principal's current state.
- Define both safety and liveness exits: no stale authority becomes valid, and
  a valid peer completes within a measured bound after quiet begins.

## Recommended first implementation slice

Start with **cross-layer destination identity**, because it is P0, remotely
reachable, and supplies a reusable observation format for later split-world and
cross-interface work:

1. Add a deterministic identity generator spanning URL authority, DNS wire
   names/answers, redirects, SNI, Host, SAN and final socket tuple.
2. Instrument the hosted client boundary to emit the authorized tuple at each
   transition without logging secrets; compare it with `connect` in a namespace.
3. Cover representation-only changes, multi-address fallback, CNAME, public to
   private transitions, IPv4-mapped IPv6 and link-local scope.
4. Run the same cases through the default and tight tiers and name every
   deliberate disagreement with external peers.
5. Add one mutant per binding check: authorize only the first address, omit
   scope, preserve old SNI, preserve old Host, or skip policy after redirect.
6. Only after a red proof, fix one invariant per commit and update
   `SECURITY_REVIEW.md` with exact evidence and remaining capability.

**First identity-harness slice implemented.** The whole-client HTTP fuzzer now
keeps a per-hop transcript of the parsed/resolved hostname, resolved IPv4
address, connected address and port, TLS hostname, and serialized `Host`
field. Every scripted redirect hop must connect to the address returned for
that exact hostname; HTTPS must pass the same hostname to certificate/SNI
handling; and the request must serialize that hostname and the connected port,
omitting only the scheme's default port. Both default and tight tiers enforce
the invariant over every generated response schedule and URL/redirect seed.
This closes stale Host/SNI and policy-address drift for this IPv4 client path,
but not the planned CNAME/multiple-address/interface-scope work: the production
resolver currently returns one IPv4 address, so those require the namespace
identity harness rather than pretending the hosted lift exercised them.

The second slice should be the control-loop graph, because it turns existing
per-transaction ceilings into a machine-wide availability claim. Entropy clone
tests come third unless inspection finds a nonce or key that is reserved only in
volatile state, in which case they become immediate P0.

## Stop conditions

Campaign IV is complete only when each P0 family is demonstrated safe with a
reproducible hostile transcript, fixed with a mutant that the test kills, or
retained as an explicit decision naming consequence and owner. P1 work may be
open, but missing hardware, kernels, corpora or measurement rigs must be shown
as evidence gaps. A large green counter, a standards citation, or “the kernel
usually rejects it” closes none of these hypotheses.
