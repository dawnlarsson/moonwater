# Security

Every byte from a command line, environment, file, kernel message and network
peer is hostile until the component that acts on it has validated it. Parsing
success is not authorization: a message must also be bound to its transport
peer, transaction, protocol state and resource budget.

This is the whole security record: model, invariants, decisions, evidence and
open gaps. It is a gap register, not a claim that the stack is bulletproof. The
honest rating is **strong deterministic coverage, incomplete high-assurance
evidence**. A gap is visible work, and a lane that did not run is "NOT RUN",
never a pass.

## Attackers

1. **Local input:** shell text, arguments, environment, files, descriptor
   layout; no privilege.
2. **Off-path network:** forged traffic, cannot see the live exchange.
   Transaction entropy and exact reply identity stop it.
3. **On-path or same-link:** observes, injects, replays, delays, reorders,
   truncates, suppresses. TLS authenticates HTTPS; plain DNS, DHCPv4 and SNTP
   do not, so their endpoint and nonce checks stop only off-path forgery.
4. **Hostile service:** owns the connected endpoint and streams bytes and
   timing forever. Bounds and absolute deadlines contain it.

## Invariants

- Length arithmetic is checked before add, subtract, multiply, align, narrow,
  allocate or copy. Elapsed time never subtracts a newer clock reading from an
  older one without ordering them first.
- A parser consumes its whole declared object or fails; no unvalidated trailing
  framing survives success.
- Every loop over hostile data makes progress under a byte, item, depth or
  absolute-time bound.
- A datagram reply matches its family, peer, transaction and echoed request
  fields before it changes state, and, at the tight tier, is exactly the size of
  the one shape the client parses (the default reads the first 48 bytes of a
  longer SNTP reply, as ntpd and chrony do).
- Security transactions use the initialized kernel CSPRNG or do not happen.
- Text is validated again at the sink that serializes or executes it.
- Failure publishes no partial output, keeps no stale authenticated state,
  leaks no descriptor across exec and leaves no secret live longer than needed.
- An ambiguous protocol spelling is refused, not read by whichever parser runs
  first. A budget (reply interval, greeting slot) is charged only for work that
  actually left; a refused send spends none.
- A side effect that selects state for a later call (an egress interface, a
  socket option) is part of that call's success.

## Decisions

- **Two tiers.** The default behaves as GNU wget and curl do. `MOONWATER_STRICT`
  at the tight tier holds RFC 9110 and RFC 3986: 204/205 framing, an explicit
  `http://` or `https://` for every URL, no redirect from public into private
  space, a whole-body deadline on streams.
- **TLS:** no revocation (OCSP, CRL, CT), as default wget and curl. SAN only,
  no common-name fallback. 120 Mozilla roots as keys, `distrust-after` dates
  enforced. DNSSEC out of scope: the resolver is a stub that trusts the
  network's DNS.
- **A refused certificate is status 5:** wget exits 5, as GNU wget does, and says
  which way it was refused in GNU's words (not trusted, owner does not match
  hostname, has expired, is not yet activated) with the `--no-check-certificate`
  hint, which still connects; any other failed handshake is 4. The causes are
  kept apart from the handshake up (`tls_conn.fault`, `TLS_UNTRUSTED` to
  `TLS_MISMATCH`, `HTTP_UNTRUSTED` to `HTTP_MISMATCH`), and a name that
  fails is named before the dates.
- **A secret on a command line (`wifi add SSID PASSWORD`, `link group NAME
  SECRET`)** is in `ps` while the command lives and in the shell's history for
  good. The standard-input forms (`wifi add SSID -`, `link group NAME -`; a
  no-echo prompt at a terminal) keep it out of both, and are what a machine
  script uses (`... - < /root/secret`). The shell's history refuses the two
  argument forms (`history_secret_line`) after assignments and a list of
  wrappers (`sudo`, `env`, `nice`, `timeout`), in the first twelve words: a
  best-effort denylist, not a boundary. It does not see `sudo -u root`, `env -u
  NAME`, `bash -c`, `ssh HOST moonwater`, a function, an alias, a variable or a
  substitution, or the verb after twelve words, and a secret on such a line is
  kept like any other word. A pairing code (`link NAME abc-def`) is left in the
  history on purpose: it works once, for five minutes, and is read off the other
  machine's screen; a link key is public. The tight tier refuses both argument
  forms; a terminal user at the default tier is told to use `-`.
- **OUT-D1, boot takes an install without asking** only when it is the disk
  the session started from (the settings an image boots with and the image on
  the disk carry the same random medium), or the only disk with this build and
  on a bus nothing is plugged into. Every copy of a public release says the same
  build, so a stick somebody pushed in said it too, and its
  `/root/main.moonwater.sh` ran as root. More than one disk with the build
  asks unless one is the session's own; one that is USB, Thunderbolt,
  `removable`, behind a `removable` PCI port, or cannot be told is asked about
  (`host_disk_external`, fail closed); the tight tier asks about every disk but
  the session's own. Default **not decided by the owner**: a lone USB stick of
  this build on a machine with no install of its own is asked about (the safe
  side), and `host_lone_taken` is the one line that flips it. The census
  holds the disk it found, so a second disk with copied partition identities
  cannot be mounted in its place.
- **Kernel defaults** init writes before it starts anything
  (`system_kernel_defaults`, src/sh/system.c): the four `fs.protected_*`
  switches, `kptr_restrict`, `dmesg_restrict`, `io_uring_disabled` and
  `randomize_va_space` (what Debian and Arch ship; the tight tier raises
  fifos, regular, `kptr_restrict` and `io_uring_disabled` to 2, widens mmap's
  randomisation to the kernel's limit and sets Yama's `ptrace_scope` 2; the
  locked tier sets Yama 3 and `modules_disabled`, both until the next boot;
  the reference tier keeps the kernel's own). They were the network
  watcher's, written a moment after boot by whichever process was that, and not
  at all where it never started. `protected_regular` and `protected_fifos` bind
  root as well: its `>file` onto a file another user made in a sticky `/tmp`
  fails, which is the attack they close; the boot, install and desktop lanes
  run with them on. `kptr_restrict` 1 and `dmesg_restrict` 1 leave root's
  kallsyms, perf and dmesg (`CAP_SYSLOG`) alone.
- **The kernel's build configuration is part of the security record.** The
  kernel is built from `allnoconfig`, where an option nobody names is off
  whatever its Kconfig default says, so a hardening option that every
  distribution takes for granted was not in the image until a profile named it
  (`sec_baseline`: stack protector, usercopy and `FORTIFY_SOURCE`, slab freelist
  hardening, `VMAP_STACK`, the x86-64 mitigations under `CPU_MITIGATIONS`,
  `W^X` on riscv64, the `mmap_min_addr` floor). Every tier composes the
  baseline; the hardened and locked tiers add five kernel regions (`sec_mem`,
  `sec_cpu`, `sec_surface`, `sec_dma`, `sec_lockdown`), each a profile of its
  own at each level, and five userspace regions that set
  `MOONWATER_STRICT_<REGION>`. The README lists what each region does. The
  gate for a region is `sh test/run switches` (what composes, what a region
  names) and a boot of the tier's image; `build verify-config` reports an
  option a tier asked for and did not get, and the build says so for every
  tier on all three architectures.
- **Mounts:** the kernel module mounts `/proc` and `/sys` `nosuid,nodev,noexec`
  and `/dev` `nosuid,noexec`, init mounts `/dev/pts` `nosuid,noexec` and
  `/dev/shm` `nosuid,nodev` (`noexec` as well at the locked tier, which also
  adds `hidepid=invisible` to `/proc`). `/` is the initramfs's tmpfs root, 1777 as the kernel makes it:
  sticky, so another user cannot replace or remove a name root made there, but
  anyone can make a new name at the top.
- **The image's own programs, built with the compiler's hardening, are a region
  (`sec_build_hardened`):** locals zeroed (`-ftrivial-auto-var-init=zero`),
  frames probed (`-fstack-clash-protection`), call-used registers cleared
  (`-fzero-call-used-regs=used-gpr`) and the stack protector with a global guard
  (`-fstack-protector-strong -mstack-protector-guard=global`; lib.util.c seeds
  `__stack_chk_guard` from `getrandom` at the top of `main` with its low byte
  zero, and `__stack_chk_fail` says so on standard error and dies of SIGABRT).
  The cost is in the profile (+5.7% on a shell start). The guard is one global,
  so a program with an arbitrary write can change it, which is the usual limit
  of a canary; the programs are not position independent (the Spark format has
  no relocations), so code is at a fixed address in every process. Held by
  `stack_guard_check` (lane `stack`: a frame that is not smashed returns, one
  that is dies with 134 and the line, two runs have different guards with a
  zero low byte) on all three architectures, and `MW_HARDEN=1` / `MW_AUTOINIT`
  / `MW_CFLAGS` in `test/run`, which build every lane's programs with the flags
  and found a `PURE` function that stored through a pointer (the redirect of
  `2>/dev/null` went to the wrong descriptor when locals were zeroed).
  `pure_stores` (kit) refuses the class: a `PURE` or `CONST` definition that
  stores through a pointer parameter, or a `CONST` one that reads through one.
- **OUT-D2, the data partition is mounted `nodev` and not `nosuid`:** it
  holds `/root`, `/home` and the bowls; a bowl's `sudo` and `su` need setuid.
  The locked tier mounts it `nosuid` as well, since it has no bowl and no one
  to run `su`. Whose disk boot takes without asking is the control (OUT-D1).
- **OUT-D3, unprivileged user namespaces stay as the kernel has them:** bowls
  need them, no tool here creates one otherwise, and `user.max_user_namespaces`
  0 would take them from the test harnesses on the same kernel; a kiosk tier
  that wants them off sets it. `unprivileged_bpf_disabled` has no knob: the
  image has no `bpf(2)` (the classic socket filters the DHCP client uses are
  separate).
- **Root is not a wall:** hardening against a hostile root is out of scope.
- **Ring 0:** no `crypto_`, `tls_` or X.509 code is in the include graph of
  `src/moonwater/core.c`; `net.c` is userspace only.
- **Residual:** DHCPv4, DNS and SNTP cannot defeat an on-path peer. C cannot
  prove ownership or bounds; guard pages, sanitizers, fuzzing, fault injection
  and three architectures compensate. Limits stop one connection from growing
  without bound, not a distributed flood.
- **ARP probe:** a new lease is installed only after three probes, 200 ms apart,
  find nobody else on the address (+0.6 s to a first lease; renewals and
  rebinds skip it). A probe that cannot be sent refuses the lease.
- **Bad URL spellings:** `http:` and `https:` without `//` are refused in every
  tier (a host named `https` is not worth a plaintext connection to the wrong
  place); the tight tier refuses every leading scheme token (`ftp:21`).

## Evidence

Run `sh test/run <lane>` or `python3 test/differential.py --harness <name>`.
`net_dependency_closure`, `net_math_proof`, `net_clock_fault`, `net_wait_states`,
`net_parser_work_states`, `net_lease_transitions` and `security_hygiene` start `lane_net`; a new shared primitive in `net.c` fails the
closure gate until it is reviewed, and `security_hygiene` fails when a harness
named here is not registered. Evidence is for one build and environment, not a
certification. `MOONWATER_REQUIRE_ARCHES=1 MW_UBSAN=1 sh test/run net` makes
ARM64 and RISC-V mandatory with UBSan trapping.

| Area | Evidence | Open |
| --- | --- | --- |
| Bytes, memory | `byte_reader` cursors (`security_hygiene` forbids listed parsers from indexing their input); `MSG_TRUNC`; guard pages (`codec`, `socket`, `standard`); ASan/UBSan fuzz `sh test/run fuzz`; MSan `sh test/run msan`; `net_math_proof` (DHCP masks, AES field and S-box, HKDF counter, netlink widths); `net_wait_states` (1,613,472 send/read and 4,302,592 writev schedules, 15,360 nested-deadline boundaries, ASan/UBSan) | `tls_parse_cert` index reads are guarded by hand and fuzzed; no corpus beyond generated seeds |
| Netlink | sender, port, sequence, alignment, multipart, `NETLINK_DISCARD_MAX`; `netlink_fuzz` | nested attributes beyond the bounded fuzz |
| DNS | exact question, ID, peer; per-query source port, 0x20, EDNS0 with fallbacks; own resolver first; `DNS_DISCARD_MAX`; `dns_fuzz`, `sh test/run net`; a validated TC reply keeps the full query deadline for TCP, while only the UDP EDNS trial is capped at one second | independent packet oracle |
| DHCPv4 | xid, MAC, server and OFFER-peer binding; option overload, END and zero padding; lease sanity; ARP probes before install, and a DHCPDECLINE (RFC 2131 4.4.1) with a ten-second hold-off for an address another station answers for; watcher cut once per link news; route ownership and rollback fault matrix (`net_lease_transitions`); `dhcp_fuzz`, `sh test/run netem net machine` | DHCP over a raw socket (`rp_filter`); the DECLINE is checked as a built packet and through its confined child, never against a live server |
| SNTP | 64-bit nonce, peer, mode, stratum, timing, exactly 48 bytes at the tight tier; each sample its own wait; `sntp_fuzz`, `sntp_era` (2036, 2038, 2104), `sh test/run netem machine` | unauthenticated; NTS is a separate branch |
| HTTP, URL | `localhost` and `*.localhost` reach 127.0.0.1 without a resolver (RFC 6761; `wget_hostile` with a stub resolver, curl held to the same rows); host spellings named against GNU wget and curl; the scheme rule at both tiers (`http_urls`); sink-side validation of host, target, headers; userinfo, schemes, fragments refused; one framing reading; redirect and HTTPS-downgrade bounds; body deadline; per-hop host, address, SNI and Host identity; `http_response_framing` (with `http.client` and curl), `wget_mutation` and `wget_hostile` (GNU wget and curl as live oracles), `https_downgrade`, `http_fuzz` | CNAME, multi-address and interface-scope identity; `inet_aton` shorthands (`0x7f.1`, `127.1`) are names here and addresses to wget and curl (decision); SNTP, logger and Waterlink peers still ask DNS for localhost; the tight tier bounds each hop, not the redirect chain |
| TLS, X.509 | transcript, Finished, AEAD, record sequence, state ordering; strict DER; CertificateEntry extensions refused; delivery schedules and FIN/RST cuts (`tls_peer --schedule`); SAN and name constraints, key usage, EKU, dates; chains against OpenSSL and Go `crypto/x509`; record nonce coverage over every value in every sequence byte lane and guarded unaligned buffers; record-header coverage over every 16-bit length and 8-bit type; `tls_chains` (also wget's status and words for each refusal), `tls_hostnames`, `tls_dates`, `public_suffixes`, `anchors`, `tls_peer`, `tls_der_fuzz`, `tls_hs_fuzz`, `tls_verify_fuzz`; `x509_corpus` by hand over 643 hosts | name constraints in more shapes |
| Crypto | OpenSSL vectors and every `lib.c` architecture body: `crypto_vectors` (`--wycheproof DIR` by hand), `crypto_fuzz` | none known |
| Wi-Fi | RSN, EAPOL-Key, replay counters, source address, scan parsing; all four GTK slots, zero-valued first keys, driver refusal/retry and roam reset (`wifi_key_state`); the strongest 64 names kept and the associated row never evicted; one BSS donates every field of a row; `wifi_scan_fuzz`, `wifi_eapol_fuzz`, `wifi_air` | management-frame protection and WPA3 (separate branch); a forger outranking every retained row |
| Waterlink | Noise handshake, cookie, replay window, grants, revocation; mDNS reads Internet class only, RCODE zero, TTL nonzero, an SRV with a non-root target and a nonzero port, exposes only a PTR and SRV intersection, and shares a packet-sized compression-pointer budget across every name; the reply and answer budgets move only after a successful send; `IP_MULTICAST_IF` failure refuses the send; a source holds at most `LINK_GREET_SOURCE` of the 16 greeting slots; legacy verifier migration is all-write plus `fsync`, and until it succeeds the listener stays off while commands keep the groups (their save is the scrub); every elapsed time is ordered first; `waterlink_fuzz` rejects a wake/fill cycle that remains immediately due without output; `sh test/run waterlink link`, `waterlink_sanitized`, `waterlink_fuzz` (MSan with `MOONWATER_MSAN=1`) | handshake flood under netem; address rotation reaches the global greeting ceiling |
| Saved state | wifi and bluetooth lists written beside themselves and renamed, opened nonblocking, regular files only; no hash of a group secret on disk; `sh test/run cli link` | |
| Secrets, boot choice | `history_secret_line` rows including what the denylist does not see (`CHECK_bowl`); `link group NAME -` and `wifi add` at a terminal (`moonwater_cli`, `link`); the tight tier's refusal of both argument forms (`CHECK_bowl` again at `MOONWATER_STRICT` 2); `host_link_external`, `host_disk_external` failing closed and `host_lone_taken` (`CHECK_bowl`); a fixed NVMe install still taken and another build still asked about (`install` lane) | a USB install in a guest (found only when it enumerates before the census, racy in QEMU); the `removable` files of a real PCI port need hardware |
| Terminal, JSON reader | `term_fuzz` (grid, write cuts, resizes, keys, pointer between writes; the cursor stays on the grid; it fails on a planted clamp bug) and `bowl_json_fuzz`, ASan/UBSan, smoke in `term` and `bowl` | KERNEL_MODE emulator build, MSan |
| Shell, OS boundary | generated Bash and Dash differential; private edit files; `edit` draws a file's controls, DEL and invalid UTF-8 as `?` and drops C1 controls spelled in UTF-8, in rows and status line (`edit` lane, `hostile`); PTY setup; tar pinned parents; hostile environment and privilege matrices; `pathname_race`; effect-based coreutils (`sh test/run shell builtins files tar`) | |
| Faults, resources | seccomp entropy failure; partial I/O, EINTR, ENOSPC, deadline and clock-jump faults; TLS 1.2/1.3 handshake writes and KeyUpdate replies share the operation deadline, including buffered writev batching; deferred read budgets are clamped to the enclosing body deadline; buffered payload cannot escape that body deadline; ordinary HTTP/TLS writes use one bounded nonblocking slow path, and queued whole writes need no clock or poll; interrupted output writev resumes its spans under one absolute retry budget, without a clock on uninterrupted writes; fatal TLS protocol errors spend both keys and cannot publish later buffered data (unexpected record types swept over both versions and sequence boundaries); descriptor and mmap exhaustion; once-armed `memory_reserve`, writev and socket faults mid-path; namespaces with netem (`sh test/run netem`) | SNTP allocation faults; a writev syscall that itself blocks on a disk or pipe is outside the interruption budget |
| Kernel (ring 0) | `core_state`, `pane_pages`, `shared_page`, `console_queue`, `term_streams` (kit lane); `ring0_hostile` on KASAN, UBSAN, lockdep or KCSAN images (`MOONWATER_IMAGE=dist/bootx64.efi sh test/run ring0`); image defaults for redirects, router advertisements, RFC 1337, SYN cookies (`sh test/run boot`, `net_sysctl`) | |
| Supply chain | bowl bootstraps pinned by digest or signing key, refusal on any mismatch; a download held to a kernel size ceiling (2 x the measured size + 64 MiB, `RLIMIT_FSIZE`); the signature reader under `bowl_sig_fuzz`; `sh test/run bowl` | |

SYN flood by hand: two taps in a namespace, a guest and a listener that accepts
and closes, 20,000 random-source SYNs a second for 14 s while a second address
connects every quarter second; toggle `net.ipv4.tcp_syncookies`. With cookies
48 of 48 answered, without 0 of 6 (use a host the kernel has not seen succeed).

`net_wait_states` exhausts five-event schedules over seven I/O outcomes,
four poll outcomes, four monotonic-clock behaviors and six span lengths.
It checks byte progress, nonblocking calls, expired budgets, bounded retries
and a clock-free complete-write path against production `wait.c`. A model
clock advances at each observation (or fails or jumps backwards); this does
not prove fairness of the kernel, hardware clocks or blocking output sinks.
The same gate lifts production `http_write_spans` and exhausts five-event
I/O scripts across four clock behaviors and 64 three-span partitions,
including zero-length spans. Nested relative/absolute deadline tests compare
expiry with 128-bit arithmetic at zero, adjacent, saturation and wrap edges,
including an aliased destination and enclosing deadline. These boundary
cases do not enumerate the entire 64-bit arithmetic domain.
`net_math_proof` separately merges equivalent bit prefixes with their exact
multiplicities over all `2^256` assignments of four 64-bit words. Under the
elapsed-time and clamp guards, the chosen inner expiry cannot exceed the
enclosing expiry; sums are compared at 65 bits, including their carry.
This proves that arithmetic lemma and checks its production guards, not C
memory safety, compiler correctness or operating-system scheduling.
Crypto stack-residue probes clear and snapshot the sampled region in assembly
on all three architectures, with planted-spill and clean-call controls,
rather than reading uninitialized C arrays.
The transport stubs in protocol fuzzers check wire shape, while the native
socket fault tests check actual deadline propagation through TLS and HTTP.

`net_parser_work_states` lifts production `dhcp_receive`, `dhcp_complete`,
peer matching and the shared discard helper. It checks 2,129,920 five-event
schedules over eight packet/transport outcomes and all 65 work boundaries,
repeating the final event to include persistent junk. Completion and receive
are compared with an independent straight-line oracle; no more than 65
datagrams are consumed to discard 64 and consider the next one. Native UDP
tests sweep every split of syntax/identity and lease-state junk at exactly
64 and 65 rejections, in both orders and renewal/rebinding states. These
regressions fail before the shared allowance: returning a parsed but unusable
packet used to reset the receive loop's local limit, allowing 4,225 packets
in the worst case. Clock deadlines still bound elapsed time independently.
The model abstracts packet parsing and lease policy; it does not prove those
policies or kernel behavior. DNS tests sweep all 127 allowed pointer depths
and work/size-boundary neighbors in all three record sections. Repeated legal
pointer chains cannot multiply decompression work across records and CNAME
passes. The allowance intentionally refuses some otherwise-valid responses
with excessive compression; it does not limit resolver traffic system-wide.
On the native x86-64 hosted parser lift (seven alternating repetitions), the
160-owner/127-jump fixture fell from a median 179.1 us to 73.2 us per parse.
A tiny compressed A reply rose from 50.7 ns to 65.9 ns; this is direct parser
cost below socket/DNS latency, but it is a measured hot-path cost rather than
a free bound. These figures are comparative host measurements, not portable
hardware guarantees.
`BENCH_net_hot` also keeps the per-record TLS wire operations visible. On the
native x86-64 host, nine interleaved runs reduced the nonce median from 12.69
to 3.26 CPU ticks by using the existing unaligned and network-order word
helpers. Eleven later interleaved runs reduced the record-header median from
3.73 to 1.09 ticks by keeping the byte swap and unaligned word store inline on
x86-64 and AArch64. The RV64 IMAFD floor keeps `lib.c`'s bytewise assembly
store because GCC otherwise emits the unavailable `__bswapsi2` runtime helper.
The production primitive checks pass on all three architectures and exhaust
each sequence byte lane and every 16-bit record length. Tick measurements are
comparative results from this x86-64 host; QEMU runs establish behavior, not
hardware latency.
Socket-fixture setup failures now stop their case, and diagnostic netlink
drains and chunk-output reads are nonblocking. A denied local send therefore
fails a check instead of leaving the security lane asleep on a byte that can
never arrive.

`sh test/run proof` (named only) holds proofs, not samples.
`net_exhaustive_proof` compares production predicates with specifications
written from the RFCs and the stated policy on every input of their domains:
`http_address_public` on all 2^32 addresses; `dhcp_mask_valid`,
`dhcp_prefix_of` and `dhcp_address_unicast` on all 2^32 values;
`dhcp_prefix_clear` on every address under every prefix length and the omitted
mask (141,733,920,768 cases); `tls_asn1_length` on every four-byte head at every
truncation; `tls_oid_content_der` up to four bytes; `http_dot_segment` up to four
bytes, and up to nine over the bytes it tells apart; the default lease timers
for every lifetime; and `dhcp_walk` against an RFC 2132/3396 walker for every
region of up to ten bytes over the bytes it tells apart, under ASan. Each
reports how many inputs were accepted and fails if either answer never occurs.
`net_bounded_proof` has CBMC prove memory safety, no pointer or signed
overflow, termination by unwinding assertion and a stated property for every
input up to a bound: `byte_reader` for buffers of any length and `byte_store`
for any append (every parser reads and writes through these), `dns_copy_name`
(7 bytes), the ASN.1 helpers (8), netlink attribute finding (24),
`http_header_end` with `http_response_fields` (10), the chunk-size and trailer
lines (10), `http_split_into` (10) and `http_path_simplify` (8: no dot segment
is left and a second pass changes nothing). Every proof is run again with its
outcome marked, so an outcome no input reaches fails as vacuous. The lib.c
routines the parsers call stand in as the contracts written in the harness.
Not proved: the X.509 parser and chain builder, the TLS handshake state machine,
`http_run`, DNS answer selection and the DHCP client's packet reader beyond its
option walk; those rest on fuzzing, differential oracles and the mutation pass
recorded with the change that added this lane.

## Ceilings

Exact-limit and one-over rows live in `CHECK_net` (`test/checks.c`, with the
ledger at the top of its section).

| Parser | Ceiling |
| --- | --- |
| DNS | message `DNS_MAX_MESSAGE`; label 63, name 255; `DNS_CNAME_HOPS` 16; `DNS_WORK_POINTERS` 4,096 compression jumps per framing section and across answer-selection passes, at most 16,384 per reply; `DNS_DISCARD_MAX` 64; `DNS_SERVERS_MAX` 3 |
| DHCP | `NETWORK_DISCARD_MAX` 64 rejected datagrams per OFFER or ACK phase, shared across packet syntax/peer and lease-state rejection; caller room |
| Netlink | `NETLINK_DISCARD_MAX` 64 unrelated datagrams per transaction; 16 MiB datagram |
| SNTP | `SNTP_DISCARD_MAX` 64 non-terminal receives per exchange |
| TLS | `TLS_RECORD_MAX`, `TLS_HS_MAX`, 64 extensions, 8 certificates |
| HTTP | `HTTP_URL_MAX`, `HTTP_HEAD_MAX`, `HTTP_FETCH_MAX` (16 MiB in memory), `HTTP_HOPS`, `HTTP_WRITE_SPANS`; `HTTP_BODY_SECONDS` (300 s) bounds a stored body in every tier and a streamed one at the tight tier, where the default keeps GNU wget's idle timer alone |
| Waterlink | 8 instances a packet; at most the packet's byte length in shared mDNS compression-pointer jumps; `LINK_GREET_SOURCE` 8 of 16 greeting slots; one reply and one answer an interval |

## A new parser or fix

Every externally reachable parser adds: empty, shortest, exact-limit and
one-over objects; truncation at every byte; every tag including unknown;
integer widths and overflow; duplicate, reordered, conflicting and trailing
fields; every stream split and partial I/O; a fixed ceiling and a test that
reaches it; identity mutations for every field that authorizes a reply; faults
for allocation, randomness, time and I/O; a differential oracle or a stated
reason for none; reads through `byte_reader`, with its name in `reader_only` in
`security_hygiene`; and a hosted lift that takes the cursor with
`byte_reader_source()`.

A security fix is complete when it records the attacker position and the
violated invariant, the reachable path and concrete input, the consequence,
the smallest fix at the shared boundary, and the residual position it does not
stop; has a family-level regression with valid neighbors that fails on the
vulnerable code (a mutant or the parent tree); and states its resource ceiling
and portability. A differential oracle can share the mistake, so name the
deliberate disagreements. A red lane is an open gap even when the fault is in
its oracle.

## Release evidence

`MOONWATER_FUZZ_REPORT=artifacts/fuzz-report.txt sh test/run fuzz` writes the
compiler and sanitizer versions, seed counts, budgets and exits (`overall_exit`
2 means NOT RUN: still attach it). Defaults match the lane smoke (20,000 runs,
5 s); `MOONWATER_FUZZ_SECONDS` and `MOONWATER_FUZZ_RUNS` lengthen it. Seeds
come from `tls_fuzz_seeds`, `dns_fuzz_seeds` and `netlink_fuzz_seeds` and are
written per run, never kept in the tree. Attach `sh test/run msan` and the
architecture gate above. CI is parked (`workflow_dispatch` only).

## Open

- **Closed, unmerged work:** NTS (#18) and the clock floor of #17 (a floor,
  SNTP_WALL_LEAST, is in the SNTP client); WPA3 and 802.11w (#19); DNS over TLS
  (#20); DHCP over a raw socket (#21); wget and curl parity of defaults (#22).
  Their branches are gone; each pull request keeps its head
  (`git fetch origin pull/N/head`).
- **No change planned:** plain SNTP is accepted anywhere the build and clock
  window allow; a forged clock step before the SNTP window is undone only by
  authenticated time; HEAD is not exercised; a response cut inside its status
  line or a header fails in both tiers (GNU wget and curl disagree with each
  other there); an endless close-delimited body at the default tier can fill the
  disk; the radio lock is held across a whole join; the elements a scan reads are
  unauthenticated, so the join only orders by what the saved network asks for and
  the handshake authenticates.
- **Evidence:** a corpus beyond generated seeds; ASan/UBSan and native
  namespace runs as required jobs; MSan beyond hosted lifts; a second oracle for
  DNS; an independent review once those are reproducible.

### Attack queue

Hypotheses, not findings. Each needs a seeded transcript, a parent-tree or
mutant red run, valid neighbors, a hard work and memory ceiling, an honest
transaction running at the same time and the layer that enforced the property.
Run only in namespaces, `mac80211_hwsim`, disposable guests and test
credentials, never against a public network.

1. **Epochs:** lease, resolver, route and interface swaps between OFFER, ACK,
   renewal and carrier loss with queued prior-transaction replies; interface
   rename and index reuse; clock jumps and suspend between steps.
2. **Scheduling:** TLS record and redirect-leg delivery torture; datagram queue
   archaeology; MTU, fragmentation and source or interface swaps at each phase.
3. **Amplification:** cheap input to expensive crypto (key shares, SAE,
   Waterlink mac1/mac2, OpenPGP MPIs); superlinear parser cost; state-table
   churn below each limiter; response bytes and CPU per input byte for every
   unauthenticated listener.
4. **Identity:** one byte string with several identities (URL, DNS, Host, SNI,
   SAN, socket); one authorized tuple used by policy, SNI, Host and `connect`;
   CNAME and multi-address fallback; DoT and NTS bootstrap names.
5. **Ambient authority:** hostile cwd, umask, descriptors, signals, terminal and
   limits at start; local races for ports and state endpoints; logs and terminal
   control sequences as sinks.
6. **Entropy:** clone, restore and fork at each random acquisition; counter and
   key rollback; never repair repetition with public values.
7. **Control loops:** cycles across DHCP, DNS, TLS, redirects, rekey and Wi-Fi
   that no per-parser limit bounds; backoff with jitter and a quiet-period
   recovery bound.
8. **Isolation:** two principals, interfaces or groups with colliding names,
   addresses and timing; descriptor and source-port reuse.
9. **Wi-Fi:** downgrade matrix, SAE abuse, EAPOL rekey collisions, beacon churn,
   multi-BSSID, a second physical chipset.
10. **Persistence:** every state path replaced by every object type; kill after
    each write, `fsync` and rename; space, inode and descriptor exhaustion;
    mirror rollback and equivocation.
11. **Kernel and NIC:** netlink with concurrent events, padding poisoned before
    syscalls, offload (GRO, GSO, checksum), UDP checksum rules, IPv6 extension
    chains; two kernels, two NIC families.
12. **Secrets:** secret-lifetime map, zeroization under LTO on every
    architecture, timing and cache oracles with predeclared thresholds.
13. **Split worlds:** middlebox, resolver and origin reading HTTP, DNS and TLS
    differently; every optional upgrade failing after partial success; version
    skew of saved state.
14. **Recovery:** convergence after the attack stops without a reboot, and no
    forgotten authenticated revocation.
