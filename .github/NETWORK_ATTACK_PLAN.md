# Network attack campaign III

This is the attack plan for the pass after pull requests 12 and 16. It is not
a claim that an item is vulnerable. It is a list of hypotheses to disprove,
with an attacker position, an invariant and evidence required before an item
may be crossed out. The existing `SECURITY_REVIEW.md` remains the findings and
decision ledger; this file is the queue for new work.

The campaign deliberately starts above packet grammar. The earlier passes have
deep parser, differential, sanitizer and protocol-conformance coverage. The
largest remaining return is in compositions: two individually valid events in
the wrong order, state retained across a trust change, resource pressure after
authentication has partly succeeded, and disagreement between the application
and the kernel or another peer.

## Rules of engagement

1. Run only against isolated namespaces, mac80211_hwsim radios, disposable
   guests and test credentials. Never direct floods or spoofed traffic at a
   public network.
2. Begin each investigation with a one-sentence invariant and the least
   privileged attacker that could violate it: same link, on path, malicious
   server, paired Waterlink peer, radio-range attacker, hostile local process,
   or compromised mirror.
3. Prefer a family generator or deterministic scheduler to one packet. Vary
   order, duplication, delay, loss, fragmentation, identity, epoch and fault
   point independently, then in pairs.
4. A finding is not closed by a green regression alone. Record a red run on
   the parent or a targeted mutant, valid-neighbor cases, a fixed work/memory/
   descriptor ceiling, architecture implications and residual risk, as required
   by `SECURITY_REVIEW.md`'s definition of done.
5. Separate application defects from Linux behavior. If the kernel terminates
   the hostile input before Moonwater sees it, preserve the packet capture and
   kernel configuration as evidence rather than crediting the application.
6. Measure availability under attack with an honest transaction running at the
   same time. “The attacker was refused” is insufficient if the honest peer was
   starved, delayed without a bound, or made to churn persistent state.

## Attack angles not yet closed

### A. Cross-protocol identity and trust transitions (P0)

These tests compose DHCP, DNS, SNTP, TLS and HTTP rather than testing each
parser alone.

- **Lease-to-name-server swap.** Change DHCP server identity, lease address,
  router and DNS server independently between OFFER, ACK, renewal, rebinding
  and carrier loss. Queue replies from the prior transaction at every boundary.
  Invariant: no datum authorized by lease epoch N changes epoch N+1, and a
  partial new lease cannot leave an old route with a new resolver.
- **DNS rebinding across a fetch.** Return public and private addresses in
  different orders, rotate them between retry and connect, change a CNAME's
  target, and rebind at every redirect including public → public → private.
  Cover multiple A/AAAA answers, a failed first connect and hostname aliases.
  Invariant: the tight tier's address policy follows every address actually
  connected to, not merely the first resolution or spelling.
- **Time-to-trust transition.** Jump realtime forward and backward while DNS,
  TLS certificate validation, NTS/plain SNTP selection, retry deadlines and
  persisted clock floors are in flight; suspend between each pair of steps.
  Invariant: protocol deadlines use a monotonic clock, authenticated time never
  loses to unauthenticated time, and a forged future cannot become a durable
  trust floor.
- **Interface and namespace churn.** Rename, remove and recreate an interface
  with the same index or name while DHCP, netlink dumps, DNS and Waterlink mDNS
  are live. Move addresses between interfaces and inject stale netlink events.
  Invariant: kernel object identity is not inferred from a recycled name/index,
  and an answer leaves on the interface whose state authorized it.

**Evidence:** add a deterministic “network epoch” namespace harness with a
seeded event schedule, packet transcript and state snapshot after every event.
Every two-event permutation must run; known dangerous triples get explicit
rows. Mutants remove the epoch/peer/interface checks one at a time.

### B. Hostile scheduling below the parsers (P0)

- **TLS delivery torture.** Split every record and handshake boundary, coalesce
  unrelated records, interleave legal compatibility CCS, tickets, KeyUpdate,
  alerts and close-notify, then FIN or RST at each byte. Add zero-window stalls,
  short writes, `EINTR`, readiness that produces `EAGAIN`, and a deadline firing
  between readiness and I/O.
- **Redirect scheduling.** Apply the existing one-byte/FIN/RST/1xx schedules to
  every redirect leg, with different address families and a connect failure on
  each candidate. Ensure intermediate bodies cannot consume the final sink's
  budget or publish partial output.
- **Datagram queue archaeology.** Before every DNS, DHCP, SNTP and Waterlink
  phase, preload the socket with valid packets from the prior transaction,
  valid packets for another interface/peer, truncations, error-queue events and
  a flood of identity-correct but state-wrong packets. Exercise receive-buffer
  overflow and `MSG_TRUNC` explicitly.
- **MTU and fragmentation changes.** Change MTU immediately before and after
  send; test minimum IPv4/IPv6 MTUs, PMTU errors, atomic IPv6 fragments, overlap
  patterns and reassembly timeout. Attribute overlap/reassembly verdicts to the
  kernel, but assert that truncation and retry cannot turn them into acceptance.

**Evidence:** extend `wget_mutation`'s schedule engine to TLS and redirect legs,
and extend `netem` with a reproducible queue/MTU family. Record a hard elapsed
time, bytes, syscalls and retry count for each case.

### C. Work amplification and asymmetric denial of service (P0)

- **Cheap input, expensive crypto.** Benchmark invalid TLS key shares and
  signatures, SAE commits/confirms and anti-clogging tokens, Waterlink mac1/mac2
  and Noise handshakes, and OpenPGP MPIs at the earliest and latest failing
  byte. Mix 99% hostile work with a continuous honest transaction.
- **Algorithmic parser cost.** Generate maximum legal nesting/item counts with
  maximum failed comparisons: DNS compression/CNAME/RR combinations, X.509
  chain candidate ambiguity and constraints, HTTP whitespace/chunk extensions,
  netlink nests, Wi-Fi information elements and OpenPGP subpackets. Plot work
  versus input bytes and reject superlinear surprises unless tightly bounded.
- **State-table churn.** Rotate source address/port, IPv6 privacy address,
  transaction ID, BSSID and Waterlink initiation identity just below every
  limiter. Fill avoid lists, replay windows, pending joins, fragment queues and
  saved-state staging names while measuring recovery after the flood stops.
- **Amplification audit.** For every unauthenticated UDP listener, measure
  response bytes and CPU per accepted input byte for spoofable and proven
  sources. Include Waterlink cookie replies and mDNS multicast/unicast paths.

**Evidence:** a resource-budget harness reports maximum CPU time, allocations,
resident set, descriptors, output/input ratio and honest-peer latency. Thresholds
are absolute enough to fail in CI, with a slower opt-in campaign for profiling.

### D. Wi-Fi state-machine abuse (P1)

- **Transition downgrade matrix.** For one SSID, rotate WPA2, WPA3, transition
  mode, PMF capable/required, RSN/RSNX element presence, AKM order and cipher
  order between scan, authentication, association and EAPOL. A security property
  learned from a successful join must not silently weaken on a later join.
- **SAE abuse.** Reflect the station's commit, reuse scalar/element pairs across
  peers, replay tokens across BSSIDs, alternate token-required and rejected-group
  statuses, send valid points with edge scalars, and reorder commit/confirm and
  association events. Time password-dependent paths statistically, not merely
  by source inspection.
- **EAPOL/rekey collisions.** Cross message 1/3 and group rekeys from adjacent
  replay-counter epochs; duplicate each around key installation; switch BSSID
  and reassociate while old frames remain queued. Assert that no key is installed
  twice, rolled back, or attached to the wrong station.
- **Discovery deception.** Exercise hidden SSIDs, duplicate SSID elements,
  multi-BSSID/RNR containers, beacon versus probe-response disagreement,
  maximum BSS counts and continuous beacon churn. The existing 64-name capacity
  finding now has a first closure: the bounded list keeps the strongest 64
  names rather than the first 64 in kernel dump order, never evicts the
  associated row, and has a full-capacity regression. The duplicate-name
  edge is closed too: association, BSSID, security, channel and signal now
  come from one BSS, so a louder unassociated twin cannot donate its metadata
  to the live association. Continuous churn,
  multi-BSSID/RNR and a forger strong enough to own every retained row remain
  campaign work.
- **Real-driver boundary.** Repeat the hwsim claims on at least two physical
  chipsets/firmwares, especially PMF, SAE offload and key installation. Treat
  hwsim-only evidence as protocol evidence, not driver evidence.

### E. Waterlink lifecycle and peer confusion (P1)

- Fix and then attack the pre-cookie per-source admission limiter already named
  in the review. Spoof a paired peer, sweep an IPv6 /64 and churn ports while an
  honest peer continuously handshakes; require a recovery bound.
- Interleave join, leave, `--forget`, rekey, roaming/NAT rebinding and process
  restart. A forgotten group must not regain authority through a live session,
  queued greeting or persisted peer record.
- Create two groups with colliding names/addresses and simultaneous greetings;
  delay the first group's answer past the second. Verify command, file-transfer
  and grant state remains bound to the authenticated group and peer.
- Exercise replay-window edges at wraparound and after long silence, duplicated
  fragments across rekey, counter exhaustion, reordered final fragments and a
  valid packet immediately after each malformed sequence.
- Attack mDNS scope: multiple interfaces, link-local addresses with scope IDs,
  multicast joins/leaves and unicast replies after address removal. The reply
  must never advertise the first interface merely because it was enumerated
  first.

**Progress.** The pre-cookie source-spoofing edge is closed by moving the
per-source bucket behind source-and-port cookie proof; unproven traffic shares
only the global/load budget, and the sanitizer lift checks both sides of that
transition. Admission-bucket time arithmetic now fails closed on a backward
monotonic reading and saturates before multiplying on an enormous forward
jump, so a clock discontinuity cannot mint tokens by unsigned wrap. The
`leave --forget`/rekey edge is held by an executable
revocation test: after the authorization record disappears, the forgotten
peer's next authenticated initiation scrubs the current, next and grace key
epochs before any subsequently queued carry is dispatched, then closes the
session. Join/leave/process-restart permutations, address-scope churn and
honest-peer latency under spoofed-source pressure remain campaign work; this
single lifecycle closure is not treated as completion of the track.

### F. Files, persistence and crash consistency (P1)

- Replace every persistent-state pathname at every syscall boundary with a
  regular file, symlink, FIFO, device, directory and mount point. Include the
  known `/root/wifi` FIFO case. All readers need a bounded, nonblocking refusal;
  writers must not publish through an attacker-chosen object.
- Kill or power-cut after every write, `fsync`, rename and directory `fsync` in
  wifi, bluetooth, leases, clock state, Waterlink groups/peers and downloaded
  artifacts. Restart two writers/readers concurrently and prove old-or-new
  state, permissions, secret scrubbing and deterministic recovery of `.new`.
- Exhaust space, inodes, descriptors and write quota after every successful
  prefix. Include an honest read while cleanup runs, and a filesystem that
  reports delayed writeback errors.
- Roll back a signed mirror to every older still-valid artifact, equivocate
  between metadata/signature/body fetches, serve sparse/oversized/compressed
  objects and change bytes after validation. Pin freshness and size policy
  explicitly rather than treating a valid signature as freshness.

### G. Kernel/application ABI disagreement (P1)

- Fuzz netlink with concurrent unsolicited events and dump replies: sequence
  wrap, multipart interruption, ACK before/after events, recycled port IDs,
  nested attributes with duplicate types and objects deleted mid-dump.
- Compile and boot against the oldest and newest supported kernel UAPI. Poison
  all struct padding before syscalls and verify returned lengths before reading;
  vary sockaddr lengths, ancillary ordering and unknown flags.
- Inject asynchronous socket errors (`ICMP`/`ICMPv6`, error queue), route changes
  between lookup and connect, IPv4-mapped IPv6 addresses, scoped link-local
  addresses and dual-stack candidate racing. Record which layer owns each
  authorization decision.
- Run the same adversarial transcript on x86-64, ARM64 and RISC-V and compare
  state snapshots, not just exit status. Add big-endian hosted builds if a full
  target is unavailable; byte order and signed-char mutants must be killed.

### H. Oracle independence and campaign durability (P2)

- Add independent oracles where the review still names none: a DNS packet/model
  oracle; a second SAE/H2E implementation; and synthetic X.509 name-constraint
  chains covering excluded subtrees, IP constraints, multiple intermediates and
  ambiguous paths.
- Preserve minimized, reviewable reproducer descriptions as generator parameters
  rather than opaque binary corpora. Also keep a content-addressed private fuzz
  corpus artifact so coverage survives releases without checking blobs into the
  tree.
- Require ASan+UBSan, MSan, native namespace/netem and at least one non-x86
  result for a release security report. A soft skip is a reported evidence gap,
  never a pass.
- Add compiler hardening and static-analysis reports, but require every warning
  suppression to name the invariant or test that makes it safe.
- After reproducible P0 evidence exists, commission a review by someone who did
  not write either the stack or these tests. Give them the open ledger and raw
  transcripts, not only the passing summary.

## Recommended first PR slice

Do not attempt all campaigns in one branch. The first implementation slice
should be **network epochs and stale queues**, because it crosses the most trust
boundaries without requiring new protocol features:

1. Build a deterministic namespace harness that schedules DHCP/DNS/SNTP events,
   interface churn and stale datagrams from a seed and emits a compact transcript.
2. Add the lease-to-resolver state matrix and prior-transaction queue cases.
3. Add clock jumps and suspend-like monotonic/boottime advancement at each wait.
4. Add MTU changes and source/interface swaps at each protocol phase.
5. Run an honest acquisition/sync continuously during hostile schedules and set
   elapsed-time, retry, CPU and packet ceilings.
6. Only then fix demonstrated invariant breaks, one commit per finding, with a
   mutant or parent-tree red run and the residual risk in `SECURITY_REVIEW.md`.

This slice reinforces current parser assurance instead of duplicating it. It
also creates the scheduler needed later for TLS/redirect delivery torture,
Waterlink lifecycle tests and Wi-Fi event reordering.

## Exit criteria

The campaign is complete only when every P0 hypothesis is one of: demonstrated
safe with reproducible evidence, fixed with mutation proof, or retained as an
explicit decision with an owner and consequence. P1/P2 items may remain open,
but their harness prerequisites and attacker model must be recorded. Passing
the existing suite alone does not close an item in this plan.
