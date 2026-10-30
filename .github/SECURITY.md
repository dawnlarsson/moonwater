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
   truncates, suppresses. TLS authenticates HTTPS, NTS (RFC 8915) the time
   when an NTS server is reachable, and DNS over TLS the names when it is set;
   DHCPv4, plain DNS and plain SNTP do not, so their endpoint and nonce checks
   stop only off-path forgery.
4. **Hostile service:** owns the connected endpoint and streams bytes and
   timing forever. Bounds and absolute deadlines contain it.
5. **Physical peripheral:** a device plugged into a port while the machine is
   on or booting (a stick, a keyboard that types, a hub, a network or audio or
   camera function, a DMA-capable bus device). It speaks descriptors and
   protocol bytes to the kernel and chooses the classes it claims. The policy
   that holds against it is `moonwater usb` (an unknown device and every
   interface of a known one stay unauthorized until a rule or the owner lets
   them in); what it cannot hold against is what the kernel does before a
   device is authorized (reading its device descriptor, the hub's own work) and
   DMA, which is the IOMMU's (`sec_dma`).
6. **Physical disk or image access:** somebody who can read or replace what is
   on the disk or the stick the machine boots from while it is off.
7. **Supply chain and update:** a changed source, firmware blob, trust anchor,
   package or update image that the build or `setup update` takes in.
8. **Compromised program:** a program of a bowl, or one started by the shell,
   that has been taken over and now acts as its own user, with whatever doors
   (sockets, FIFOs, files, ioctls) the machine leaves open to it.

Attackers 1 to 4 are covered by the evidence below. 5 to 8 were added with the
air-gap work (2026-10-26) and are being audited in that order; the audit's
findings are logged in `incident_response.md` as they are fixed, and each
attacker gets its invariants here once there is a lane that proves them.

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
  longer SNTP reply, as ntpd and chrony do; an NTS reply is longer by design,
  is parsed whole at every tier and believed only when its authenticator opens,
  and one that fills its 1,280-byte room is cut and is noise).
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
  space, a whole-body deadline on streams. For time and names the tight net
  tier (`MOONWATER_STRICT_NET` 2 and 3, the `sec_net_hardened` and
  `sec_net_locked` profiles) defaults to NTS only and DNS over TLS only: no
  plain time is taken and no name leaves in the clear unless root's
  `/root/ntp.nts` or `/root/dns.tls` says otherwise (a written word wins over
  the tier). The cost is a machine that has no clock until an NTS server
  answers (TCP 4460 and UDP 123 to Cloudflare, Netnod or PTB, or the servers
  named), and whose wget, fetch, `host` and zone question resolve nothing
  until then: the DNS over TLS certificate is judged against the clock, and
  NTS's own names are asked in plain, so the one bootstraps the other.
  `nts_mode_of` and `dns_tls_mode_of` are the two lines that choose, and
  CHECK_net (built at both tiers), `nts` and `dns_tls` hold them.
- **The clock has a floor.** A clock earlier than `MOONWATER_CLOCK_FLOOR` (the
  date of the source, net.c, which the SNTP client names `SNTP_WALL_LEAST`;
  `tls_dates` holds it not ahead of the machine's clock and within 120 days of
  the newest commit) or than `/root/clock.good` (the latest time the machine
  knew for certain) is unset, not wrong: certificates are not judged against it
  (wget says so in its own words, status 5, and names `moonwater time sync`),
  `moonwater time` says so, and no NTP answer earlier than the floor is taken. A
  `clock.good` that is not a plain file holding a decimal number of seconds
  between the floor and the end of the NTP window (2104) is ignored, so a
  damaged or planted one can only fail to help; deleting it is always safe. It
  is a lower bound nothing forged can move: an authenticated answer's own time
  (kept as the wall second it set and the boot second, so a later step of the
  wall clock changes nothing), or else the floor the boot began with plus the
  seconds since, never the wall clock; it is written every six hours of uptime
  and when the machine stops, forward only, never through a link, and `wipe
  yes` keeps it. A program that cannot read `/root` judges by the build's floor.
- **NTS is asked first; plain SNTP is the marked fallback.** The NTS-KE
  connection is the TLS 1.3 client with ALPN `ntske/1` (1.3 alone on offer)
  and the RFC 8446 exporter; the AEAD is AES-SIV-CMAC-256 on the tree's own
  AES. The one connection whose certificate is not judged by the clock is the
  key establishment, until an authenticated answer has been taken this boot: it
  is judged as of the floor with `notBefore` not asked (an RTC two years out
  reads every certificate expired), as chrony's `nocerttimecheck` does but with
  the floor for `notAfter`. An authenticated answer is believed anywhere the
  window puts it; a step past two seconds on a set clock takes two NTS servers.
  With no NTS server reachable the pool is asked, marked plain in `moonwater
  ntp`, never written to the floor, and refused when it puts the clock further
  from this boot's NTS time than 5 s and 1000 ppm; `moonwater ntp nts only`
  refuses plain time altogether, a server named in `/root/ntp.server` included.
  NTS-D1: `AEAD_AES_128_GCM_SIV` is not offered (RFC 8915 makes SIV-CMAC
  mandatory and every public server has it). NTS-D2: plain NTP stays the
  default fallback at the default tier, 1:1 with a machine without NTS; a
  network that blocks TCP 4460 costs one try and two minutes without another.
- **DNS over TLS is off by default (DOT-D1).** `moonwater dns tls on` asks the
  resolvers known to offer it (1.1.1.1, 1.0.0.1, 9.9.9.9, 149.112.112.112: the
  ones resolv.conf names, or else all four) over TLS on 853 first and falls
  back to UDP within two seconds of the first try; `only` sends no name in the
  clear, the network's own resolver included, so a name only that resolver
  knows does not resolve. The certificate is checked as for https, the
  resolver's address against an iPAddress name. A captive portal answers or
  drops 853 and a first lease arrives with the clock unset, both an ordinary
  first minute, which a default should not charge two seconds a lookup for.
  Only the HTTP client and `host` follow it; NTS, SNTP, a caIssuers fetch,
  hostid, logger and waterlink ask in plain (a clock cannot wait for names
  that need it, and a lookup inside a handshake opens no handshake). The word
  is kept in `/root/dns.tls` and published in `/run/moonwater/dns.tls`, so a
  lookup by a user who cannot read `/root` follows it too.
- **TLS:** SAN only, no common-name fallback. 120 Mozilla roots as keys,
  `distrust-after` dates enforced. DNSSEC out of scope: the resolver is a stub
  that trusts the network's DNS (DNS over TLS authenticates the resolver, not
  the answer's origin).
- **Certificate Transparency at the tight tier (`tls_ct_holds`):** a chain
  that ends at a public root has to carry its leaf's proof of being logged,
  as Chrome asks: the SCTs embedded in it (RFC 6962), each verified under the
  log it names in `src/net/ct_logs.inc` (Chrome's list, regenerated by
  `--harness ct_logs` from the list its header names) over the
  precertificate -- the leaf's TBS without the SCT list, and the SHA-256 of
  its issuer's SubjectPublicKeyInfo -- and counted only from a log that is
  usable, qualified or read-only, or retired after the SCT was made, whose
  temporal interval holds the leaf's notAfter, and only if made by now.
  Counted SCTs come from two log operators at least, one from a log not
  retired, two for a leaf of 180 days or less and three for a longer one.
  Otherwise wget says the certificate "was not logged as Certificate
  Transparency asks", status 5. SCTs sent in TLS or in an OCSP staple are not
  read (every public certificate carries its own). The default and reference
  tiers do not ask, as GNU wget and curl do not. The table is a snapshot:
  logs Chrome adds after it are unknown here until it is regenerated, so a
  leaf logged only to newer logs is refused at the tight tier.
- **Revocation is stapling and CRLs, in tiers** (`tls_revocation_holds`). A
  checked connection offers `status_request`; the leaf is asked by the OCSP
  response its server staples (the leaf's CertificateEntry in TLS 1.3, a
  CertificateStatus after the Certificate in TLS 1.2, relaid into the same
  entry so one check reads both), and without a good staple, like every
  certificate above it, by the CRL its first whole-reason
  distribution point names, at each of its `http:` URIs in turn until one
  answers (Microsoft's first, www.microsoft.com, answers a plain GET with 403;
  its second, crl2.microsoft.com, serves the CRL). A staple has to be signed by the issuer or by a
  responder the issuer issued with `id-kp-OCSPSigning`, name the leaf by a
  SHA-1 or SHA-256 CertID, and be fresh within five minutes either way of
  `thisUpdate` and a `nextUpdate` it must carry; a CRL has to be the
  issuer's (Name, authority key identifier when both carry one, `cRLSign`
  when the issuer limits its key, signature), cover the certificate
  (`issuingDistributionPoint` sharing a name with its distribution point,
  of its kind, not partial or indirect) and be fresh the same way. **Revoked
  refuses in every tier**, stale or not. A must-staple leaf (TLS Feature
  `status_request`, RFC 7633) without a good staple refuses in every tier. A
  source that gives no usable answer -- unreachable, too large, unparseable,
  stale, signed by no one it may be, about another certificate, `unknown`,
  `tryLater` -- is **passed over at the default tier and refused at the
  tight one** (`MOONWATER_STRICT_NET >= STRICT_TIGHT`, the hardened and
  locked `sec_net` profiles). A certificate that names no CRL and whose server
  staples nothing has no source to fail and passes in both. OCSP is never
  fetched, only stapled, so a responder URL alone is not a source: by
  decision, not omission. The CA/Browser Forum's Baseline Requirements have
  had every publicly trusted CA publish CRLs since 2024, Let's Encrypt
  stopped answering OCSP in 2025, and a request to a responder tells it
  which site this machine is visiting, which a CRL, fetched whole, does not. A CRL is fetched
  under the caIssuers rules (`http:` only, port 80 unless a harness serves
  it, a public address, one HTTP/1.0 GET, no redirect) inside one
  ten-second budget per handshake that nests in the handshake's own, held in
  memory for the process (no file: the fetch may run where none can be
  opened), refetched past its `nextUpdate`, and a failed fetch is not tried
  again for five minutes. `--no-check-certificate` asks for nothing and reads
  nothing, and keeps refusing an unasked staple.
- **A refused certificate is status 5:** wget exits 5, as GNU wget does, and says
  which way it was refused in GNU's words (not trusted, owner does not match
  hostname, has expired, is not yet activated, has been revoked -- GNU wget's
  GnuTLS line) with the `--no-check-certificate` hint, which still connects;
  where GNU wget has no line, a status that could not be checked where it is
  required says `ERROR: Whether the certificate of 'HOST' is revoked could
  not be checked.` with the same hint. Any other failed handshake is 4. The
  causes are kept apart from the handshake up (`tls_conn.fault`,
  `TLS_UNTRUSTED` to `TLS_UNCHECKED`, `HTTP_UNTRUSTED` to `HTTP_UNCHECKED`),
  a name that fails is named before the dates, and revocation is asked only
  of a path that passed everything else.
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
- **A Waterlink group's grants are its members' grants, taken back together**
  (`link_group_join`, MW-2026-040). A machine paired through a group carries
  the group's grants copied into its own peer record; narrowing the group
  (`link group NAME SECRET allow GRANT...` with a bit dropped) clears the
  dropped bit from every member the group paired, under the peers lock, and
  writes the members before the group so a half-written change grants less,
  never more. A grant the group starts giving is not pushed onto members
  already paired (each is widened only by name); a peer paired by hand (group
  mark 0) is never touched. Before the fix a group grant was a pair-time
  snapshot that revocation did not reach, so a machine the operator had taken
  `run` from kept running commands as root. A machine is never called by a
  word `link` uses, the `port` verb included (MW-2026-041).
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
  `MOONWATER_STRICT_<REGION>`. The Security tiers section below lists what each region does. The
  gate for a region is `sh test/run switches` (what composes, what a region
  names) and a boot of the tier's image; `build verify-config` reports an
  option a tier asked for and did not get, and the build says so for every
  tier on all three architectures.
- **The pinned kernel source carries its own security fixes, and a tripwire
  holds the pin to them.** The image is built from one Linux tree (currently
  7.3, `artifacts/linux-7.3.tar.xz`, `git archive` commit `c03c1a14`); Moonwater
  touches networking only through the read-only `mw_net_silent_gate` RST/ICMP
  guard in `kernel/kernel.c`, which alters no free. A fetch that moves the pin
  forward must keep the fixes the pinned tree already carries: the `kernel_cve`
  row of `sh test/run kit` reads the tarball (or the extracted tree) and fails
  if the IPv4 TCP connection-setup options (`inet->inet_opt`, `ireq->ireq_opt`)
  revert to a plain `kfree()` from the `kfree_rcu()` that the fix for
  CVE-2026-98432 installed (`e3f33b0a1d89`, shipped in 7.2.10 and 7.3). The
  verdict and a deferred KASAN reproduction recipe are in `incident_response.md`.
- **Mounts:** the kernel module mounts `/proc` and `/sys` `nosuid,nodev,noexec`
  and `/dev` `nosuid,noexec`, init mounts `/dev/pts` `nosuid,noexec` and
  `/dev/shm` `nosuid,nodev` (`noexec` as well at the locked tier, which also
  adds `hidepid=invisible` to `/proc`). `/` is the initramfs's tmpfs root, 1777 as the kernel makes it:
  sticky, so another user cannot replace or remove a name root made there, but
  anyone can make a new name at the top.
- **The compositor's doors are root's (`WINDOWS_DOOR_MODE`, MW-2026-020).** The
  compositor runs as root and frees a window's row when every writer of its
  FIFO door (`/run/moonwater/window.N`, and the wake beside it) has closed, so
  a door the whole machine could write was a door any local program could hold
  open to keep a dead window's row forever and, after thirty-two, deny the
  machine a new window. The window table is the compositor's `memfd`, reachable
  only through `/proc/PID/fd` of a root process, so no non-root program can map
  it and be a window; the doors are now `0600`, which every client (a program a
  desktop session starts, root, and the compositor) opens as root. An
  unprivileged window client, if ever wanted, is handed the door and the table
  as descriptors, never a world-writable FIFO.
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
- **OUT-D4, a word a disk's `/root` chose is spelled, not obeyed, when a root
  verb prints it.** `moonwater keyboard`, `timezone`, `ntp` and `status` print
  words read from `/root` (`/root/keyboard`, `/root/timezone` and its mode,
  `/root/ntp.server` and `/root/ntp.nts.servers`). Those sinks spell each word
  through `writer_terminal_name`, which escapes control bytes, tab and high
  bytes to `\xNN` in the formatter, so a disk (attacker 6) whose word carries a
  newline cannot forge a second `[Moonwater]` line and a bidi override cannot
  reorder one; `plain_out` keeps a `%s` field's own newline and tab on purpose,
  which is why the escape is per field and not in the stream. A legitimate code,
  zone name or hostname is ASCII and is unchanged. The other verbs that print a
  disk's or a device's word already route through an escaper (`radio_display`
  for SSIDs, `usb_shown`/`usb_key` for USB strings, `link_name_good` for peer
  names, `name_valid` for the machine name, `host_plain_line` for an image's
  build). Found and fixed 2026-10-26 (MW-2026-030).
- **OUT-D3, unprivileged user namespaces stay as the kernel has them:** bowls
  need them, no tool here creates one otherwise, and `user.max_user_namespaces`
  0 would take them from the test harnesses on the same kernel; a kiosk tier
  that wants them off sets it. `unprivileged_bpf_disabled` has no knob: the
  image has no `bpf(2)` (the classic socket filters the DHCP client uses are
  separate).
- **Two switches for bowls, `moonwater security packages` and `sudo`.** They
  are kept in the security list of the image's settings and published in
  /run/moonwater/security beside TLS's five (switch ids 6 and 7; TLS_SWITCHES
  is 7, so a TLS client reads the file past them), and `security_switch_on`
  (host.c) answers them for bowl.c and the command's root check. `packages`
  (the programs of a bowl may run the package manager of their distribution)
  is on at the default tier, as it always was, and off at the bowl's tight
  tier (`MOONWATER_STRICT_BOWL` 2 and 3: `sec_bowl_hardened`,
  `sec_bowl_locked`, and `sec_hardened` and `sec_locked`, which put every
  region there), which refuses `packages on`: it only tightens, and off is
  meant to take from a compromised bowl program (attacker 8) the one tool
  that installs more programs. `sudo` (the `moonwater` command
  through sudo or su in a bowl, by a process that is root only inside the
  bowl's user namespace) is off at every tier and can be turned on at the
  default tier only; a tight build refuses `sudo on` and reads one kept by
  another build as off. It is off because on trusts root in a bowl as root of
  the machine for the command's verbs, and root in a bowl is whoever can start
  one, the user and not a second account, so on hands every such user the
  machine's settings: it is for a machine with one user. It cannot hand over
  privilege. The root check is the command's manners, and whatever changes
  the machine is the kernel's to allow (`capable()`, in the first user
  namespace): a process that owns its mount namespace can plant a file of its
  own at /run/moonwater/security and read sudo as on, and gains nothing the
  kernel would not give it anyway. The reader takes the file only as a
  regular file, not through a link, that a group and others may not write,
  owned by root or by the overflow uid (which is how root's file looks from a
  user namespace that does not map root, a bowl's), no longer than the
  overlay and read whole; anything else is the tier's default, never sudo on.
  A bowl has a /run of its own, so packages is asked before a bowl's
  namespaces are made, and the command inside a bowl sees the switch only
  when the bowl carries the file in. An older build that boots settings with
  either switch turned publishes the entry and refuses the file whole, since
  its reader knows five ids, so its TLS refuses every checked certificate
  until a build that knows the switch sets it back. What asks the switches
  is a change of its own: bowl.c before a bowl's program runs a package
  manager, and the root check before it takes a bowl's root. Until it lands
  a switch is kept, published and listed and enforces nothing, and that
  effect is a gap no row holds. The switches and their reader are held by
  `moonwater_cli` (defaults, a turn and back, the published entries, a
  second run, nobody, nine files that do not count, the tight build's lines
  and refusals) and by `CHECK_bowl`'s `switches_asked` at both tiers (`sh
  test/run cli bowl`).
- **BOWL-D1, who a bowl's programs are against attacker 8 (`bowl_become_user`,
  src/bowl.c; MW-2026-010).** A bowl's programs run in a user namespace. At the
  default tier the user's inside id 1000 is mapped to the launcher's outside id,
  which is 0 when root launches the session (the Canvas terminal, the console,
  the `/bowls/bin` launchers, the KDE session): a program of the bowl is then
  the machine's root to the kernel (every file it makes is root's, every file
  root owns is its own, a signal it sends is root's) and the fast view keeps the
  host's `/root` and `/run/moonwater`. This was deliberate (`6b81fbd2`, so a
  desktop's home, devices and runtime directory "need no other mode") and is 1:1
  with before, but under attacker 8 a taken-over browser or KDE component is
  unconfined root (it reads `/root/link.key`, writes `/root/main.moonwater.sh`
  to be run as root, signals any root process). The **hardened bowl tier**
  (`MOONWATER_STRICT_BOWL >= STRICT_TIGHT`, `sec_bowl_hardened` and the locked
  tier) drops a root-launched fast launch to a real unprivileged kernel id 1000
  before the namespace, in the device groups a compositor needs (`video`,
  `render`, `input`, `audio`, `tty`, not `disk` or `kmem`), with
  `NO_NEW_PRIVS` and only the namespace's capabilities (which reach nothing the
  machine owns, since root is unmapped), binds a home the user owns over `/root`
  and gives it its own `/run/user/1000`; it fails closed. `bowl setup`,
  `bowl --root`, `bowl udev`, `bowl bus` and a bare `bowl ROOT` console shell
  stay root in the bowl's own tree. Held by `lane_desktop` under
  `MOONWATER_BOWL_TIER` (three confinement rows and the existing weston draw
  rows). Default **not decided by the owner**: whether to make the dropped id
  the default (it moves a session's config from `/root` to `/home/user`), which
  is the behaviour change `6b81fbd2` deferred.
- **The clients' network half runs apart (`net_apart`, `src/net/net.c`).**
  wget (`--spider` too), fetch, `host`, the NTS key establishment and every
  SNTP or NTS time query the machine's NTP child makes as root
  (`nts_ke_apart`, `sntp_query_apart`), the zone question the machine asks
  at boot as root (`locale_auto_ask`) and the plain lookups of hostid,
  logger, a Waterlink service and NTS's time server (`net_resolve_v4_apart`)
  parse TLS, X.509, HTTP, NTP and
  DNS from whoever answers, so that half runs in a forked child; bowl's
  downloads exec wget and get it too, and a DNS over TLS lookup or a
  revocation list a fetch needs is made inside the same child.
  The child keeps the answer's pipe, the output it was handed (wget's file or
  standard output, or nothing) and standard error, and closes the rest; reads
  `resolv.conf`, the DNS over TLS setting and the clock's floor
  (`clock.good`) before it may not (the anchors are compiled in, the clock is
  the vDSO, a revocation list is held in memory); becomes nobody when it is root (a user namespace that maps no
  nobody, `unshare -r`, keeps its root, which owns nothing outside it); is
  not dumpable, dies with the client (`PR_SET_PDEATHSIG`) and gains nothing
  through exec (`NO_NEW_PRIVS`); then a seccomp filter kills it at any call
  but IPv4 TCP and UDP sockets (no unix, netlink, packet, raw or IPv6), their
  connect, bind, sendto and recvfrom, `SO_RCVTIMEO`, `SO_SNDTIMEO` and
  `TCP_NODELAY` to set and `SO_ERROR` to read, ppoll, the clock, getrandom,
  write, writev, close, memory that is never executable, exit, and
  `riscv_hwprobe` on RISC-V; a time query's child may also `recvmsg` and set
  `SO_TIMESTAMPNS` and `SO_TIMESTAMPING`. A redirect, a caIssuers fetch or a revocation
  list is another socket in the same child. The client opens and publishes
  the output and makes every decision that touches a file, and believes the
  child's answer only as far as it can check it: a status `http_run` can
  return, an errno, a URL, head and body no longer than they may be, lengths
  that add up; the zone question's answer is read again through the zone
  table, so what root writes and logs is the table's spelling. **It fails
  closed:** a kernel that will not take the filter (one without seccomp, a
  container whose filter refuses it, qemu-user) fetches nothing
  (`HTTP_CONFINE`; wget exits 1, "cannot confine the download"); a child
  that dies or answers what cannot be believed is `HTTP_APART` (wget exits 4
  and names the signal); a child killed by SIGPIPE or SIGXFSZ died writing the
  output, and the client dies of the same signal, so `wget -O - | head` is
  still 141. `host` says "cannot confine the lookup" (`DNS_CONFINE`) or that
  the confined lookup died (`DNS_APART`), and an NTS key establishment that
  cannot be confined is no time from that server. The NTS state comes back
  only as `nts_ke_reply` could have made it (one to eight cookies of 1 to 256
  bytes, a name or nothing for the time server, a port not zero), and the
  keys are wiped from every copy but the caller's. A time query's answer is
  believed as `sntp_query_at` could have made it: a status it returns, a
  sample `sntp_reply_sample` could have taken (delay within 2 s, an offset
  inside the NTP window and inside the asked bound once synchronised), and
  for NTS only the cookies, each 1 to 256 bytes; `moonwater time sync` says
  when the query could not be confined (`SNTP_CONFINE`) or died
  (`SNTP_APART`). DHCP has its own filter
  (`dhcp_confine`, DHCP-D2), which also fails closed. A new client is a task
  handed to `net_apart`, and `security_hygiene` fails any call into the
  HTTP/TLS engine, the NTS key establishment or the resolver over TLS that is
  not inside one.
- **Root is not a wall:** hardening against a hostile root is out of scope.
- **Ring 0:** no `crypto_`, `tls_` or X.509 code is in the include graph of
  `src/moonwater/core.c`; `net.c` is userspace only.
- **Residual:** DHCPv4, plain DNS and plain SNTP cannot defeat an on-path peer. C cannot
  prove ownership or bounds; guard pages, sanitizers, fuzzing, fault injection
  and three architectures compensate. Limits stop one connection from growing
  without bound, not a distributed flood.
- **ARP probe:** a new lease is installed only after three probes, 200 ms apart,
  find nobody else on the address (+0.6 s to a first lease; renewals and
  rebinds skip it). A probe that cannot be sent refuses the lease. Once the
  address is in, two ARP announcements 100 ms apart (RFC 5227 spaces them two
  seconds) refresh the neighbours' caches.
- **DHCP-D1, the replies of an acquisition come off a packet socket,** so
  `rp_filter` is on: 2 (loose) at the default tier and 1 (strict) at the
  tight one, with `arp_ignore` 1, `arp_filter` 1, `arp_announce` 2 and
  `arp_notify` 1. A UDP socket never got the OFFER of a server the machine
  has no route to yet with either `rp_filter` set, nor a reply unicast to the
  offered address. The packet socket sees frames before netfilter and
  `rp_filter`, so what IP and UDP would have refused is refused by
  `dhcp_unframe` (version, header length, total length, fragments, protocol,
  both checksums unless the kernel vouches for the UDP one, ports, UDP length)
  and the destination must be the broadcast address or the one offered; a
  locked classic BPF prefilter keeps only UDP to port 68 that is not a later
  fragment. The IP source only binds the exchange to its OFFER's sender, as
  the UDP peer did. Renewals and rebinding hold an address and stay on UDP.
- **DHCP-D2, no filter, no exchange:** the confined child refuses to run when
  seccomp will not install its filter (a kernel without `SECCOMP_FILTER`, which
  every Moonwater kernel has by `build_essentials`, or qemu-user): no lease
  rather than hostile packets parsed unfiltered. Its `sendto` and `write` may
  name only the client socket and the answer pipe, since a packet socket that
  can be written sends any frame. Callers that are not the watcher's child
  (the checks) are not confined.
- **DHCP-D3, a REQUEST is let finish:** link news, the wake pipe or the end of
  the twelve-second budget while a REQUEST is in its ACK window waits up to two
  more seconds for the answer, the news unread until then (and through the
  conflict probe after it). When the grace runs out the address is given back
  with a DHCPRELEASE broadcast from 0.0.0.0 (ciaddr the address, the server
  named, no parameter list). A window that ended unanswered gets no grace and
  nothing is released for it.
- **FW-D1, a closed port is silent off loopback at the tight net tier
  (`net.ipv4.moonwater_silent`):** a TCP segment to a port nothing listens on is
  answered with a reset and a UDP datagram to a closed port with an ICMP port
  unreachable, which tells a scanner the port is closed and is work done per
  packet for whoever sent it. The switch (an int, `mw_net_silent_gate` in
  `kernel/kernel.c`, written 1 by the network watcher at `STRICT_NET` 2 and 3,
  left at the kernel's 0 at the default tier) has the four "no socket" ends of
  the receive paths drop the packet with no answer unless it came in on a
  loopback device: TCP skips the no-socket `tcp_v{4,6}_send_reset`, UDP skips
  the port-unreachable `icmp_send`/`icmpv6_send`, and the packet is dropped
  with the reason it would have carried after answering. Loopback keeps
  answering, so a connect to a closed port of `127.0.0.1`, `::1` or the host's
  own address (routed over `lo`) is still refused at once; the check is by
  device, not by address. It is not a firewall: a port with a listener is
  reached as ever, and nothing is added to the path of a packet for an open
  port (the checks sit only on the cold no-socket paths, past the checksum).
  Honest limits: a reset or an error for a packet that matches an existing
  socket or TIME-WAIT is untouched, as is every other ICMP, neighbour
  discovery, Packet Too Big and the ICMP errors of forwarded traffic, so the
  host still answers an echo and an ARP or ND and goes on being visible, and an
  open port's reply still stands out against the silence of the closed ones.
  The switch is written only by the network watcher when it starts, so a
  tight-tier machine whose watcher never ran keeps answering, and a value set
  by hand is undone the next time the watcher starts; it is one int in the
  first network namespace's table (`ipv4_table` is registered for `init_net`),
  so a namespace of its own neither sees nor flips it, while a packet reaching
  the host over a veth from another namespace is silenced by that one int like
  any other. A `MOONWATER_STOCK` kernel has no such file. The observable is that
  a scan stops moving `/proc/net/snmp`'s `TcpOutRsts` and
  `Icmp`/`Icmp6OutDestUnreachs` (`TcpInSegs`, `UdpNoPorts` and `Udp6NoPorts`
  still count the packet): proved between two guests on a private link by
  `sh test/run firewall`.
- **Bad URL spellings:** `http:` and `https:` without `//` are refused in every
  tier (a host named `https` is not worth a plaintext connection to the wrong
  place); the tight tier refuses every leading scheme token (`ftp:21`).
- **WIFI-D1, TKIP and WPA1 are never joined:** a group cipher of TKIP (the
  WPA/WPA2 mixed setting) or a pairwise set with no CCMP is refused before
  anything is sent, in words that name the fix ("set the access point to
  WPA2 or WPA3 with AES (CCMP)"); any other cipher is refused as not offered.
- **WIFI-D2, what a join asks for (`wifi_join_plan`):** WPA3 (SAE, group 19)
  whenever the access point offers it and the saved secret is a password (a
  key of 64 hex digits is a PSK and cannot do SAE), with management frame
  protection required; WPA2 (PSK-SHA256 when it is offered with protection,
  else PSK) when it is not, with protection whenever it is offered. A network
  that offers both is tried as WPA2 after WPA3 only when the exchange came to
  nothing for a reason that is not the password. What the join reads it from
  is both frames the kernel keeps of the access point (its latest, which a
  probe response from anybody can be, and its last beacon), taking what either
  offers: a forged frame can then add an offer, which costs a join a forger
  could stop anyway, but not take one away. Message 3 of the handshake carries
  the access point's own RSN element and RSN extension element under the MIC;
  one that matches neither frame the join chose it from is refused unanswered
  (`WIFI_REFUSE_ELEMENTS`, 802.11-2020 12.7.6.4, as wpa_supplicant does), so a
  stripped offer is caught at the handshake.
- **WIFI-D3, protection is asked for as required:** the kernel takes no
  optional form from a driver with no connect of its own (mac80211's kind), so
  a join that sees protection offered asks for `NL80211_MFP_REQUIRED`, and the
  kernel then drops unprotected deauthentication and disassociation frames.
  The IGTK (BIP-CMAC-128, key ids 4 and 5) is installed as the GTK is: only
  on a link that asked for protection, only from a frame whose MIC checked,
  once per key (zero bytes are a key; `igtk_installed`), before the group
  handshake is answered or its replay counter consumed, and dropped on a roam.
- **WIFI-D4, what a network teaches is kept (`/root/wifi.rules`, 0600, written
  beside itself):** an access point of a saved name that offered protection
  makes every later access point of the name without it refused
  (`WIFI_REFUSE_PMF_GONE`), and one joined with WPA3 makes WPA2 refused
  (`WIFI_REFUSE_SAE_GONE`): a twin that offers less is what a forged
  deauthentication or an offline password guess needs. The choice of access
  point ranks such twins last. Both are forgotten by `wifi remove` and by
  `wifi add` with another password (its owner saying the network changed),
  and kept by an add with the same one, which a machine script makes at every
  boot. Trust on first use: the first join of a network has nothing to hold it
  to but message 3.
- **WIFI-D5, the tight tier (`MOONWATER_STRICT_NET >= STRICT_TIGHT`, which
  `sec_net_hardened` and `sec_net_locked` select):** every secured join has
  protected management frames, so a WPA2 network without them is refused
  (`WIFI_REFUSE_NO_PMF`) whatever the saved network says, and WPA2 is joined
  only for a network saved with `moonwater wifi add SSID - wpa2`
  (`WIFI_REFUSE_WPA2`), with no fall back from WPA3 to WPA2 without it. The
  owner's `wpa2` changes nothing below the tight tier. Default **not decided
  by the owner**: whether `wpa2` should lift the protection requirement too
  (one line in `wifi_join_plan`).
- **WIFI-D6, SAE's password element and side channels:** hash-to-element
  (12.4.4.2.3: PT by two simplified SWU maps of RFC 9380, Z = -10, square roots
  by the fixed exponent (p + 1) / 4, choices by mask) whenever the access point
  says it takes it in its RSN extension element, which every 6 GHz network and
  every one set to it alone does; hunting and pecking otherwise, as every WPA3
  station has: each pass is worked out whole, at least 40 of them (255 at
  most) whatever pass lands, the test for a square and the choice of the root
  and of the pass by mask and fixed exponent, the Dragonblood advice. What the
  password still decides: the HMAC's block count (its length), and, below 2^-40,
  more than 40 passes. The secret scalar multiplications are
  `crypto_point_scalar_private`; the peer's commit is checked for range, the
  curve and reflection before any multiplication. Not built: password
  identifiers, groups other than 19 (a refusal of group 19 ends the join),
  SAE-PK, the extended-key suites (AKM 24 and 25), FT. A network that offers
  hash-to-element is never hunted for: the extension element of message 3 is
  held to what was seen, so stripping it from the beacon is caught.
- **WIFI-D7, the station runs apart (`radio_join_apart`, src/sh/host.c):**
  the scan, the choice, WPA3's exchange, the association, the four-way
  handshake and, for as long as the link stands, the keeper's rekeys run in a
  station process that is nobody with CAP_NET_ADMIN and CAP_NET_RAW and no
  other capability (the rest dropped from its bounding set, nothing ambient),
  not dumpable, killed with its broker, with no new privileges and under a
  seccomp filter (`radio_filter_build`): netlink sockets of the route and
  generic families, the EAPOL packet socket (datagrams of type 0x888e) and
  nothing else, a netlink group joined or left and extended acks as its only
  options, never executable memory, no file, no exec. The scan elements, SAE
  commits and EAPOL-Key frames any station in range sends are read there.
  What it needs of the machine's state (what a saved network taught, the
  access points to try last, a scan's age, the WPA3 budget) it asks of its
  broker as fixed-size questions by number; the broker is root, detached,
  holds the keeper's lock, does the one thing each question names to the one
  file it names, logs only its own literal for a numbered line, and believes
  only a status a join can return. Every password the joining process holds
  is wiped from the broker's copy at once and from the station's once its
  join is done. **It fails closed:** a kernel that will not take the filter
  joins nothing (`WIFI_REFUSE_CONFINE`, said by `moonwater wifi`); a station
  that ends without a status is `WIFI_REFUSE_APART`.
- **The Waterlink listener's mDNS reader runs apart (`link_mdns_read_apart`,
  src/waterlink/nearby.c):** every host on the link may send the listener
  mDNS, and reading it walks names, compression pointers and records of the
  sender's lengths. The listener is root and keeps the link's key, so the
  reading (`waterlink_mdns_read`) is done by a child that is nobody, not
  dumpable, dies with the listener, holds only its end of a socket of whole
  messages and stdio, and is killed at any call but reading a packet from
  that socket and sending the reading back. The listener believes the
  fixed-size reading only with every count and length inside its array; a
  reader that dies or answers what cannot be believed is ended and started
  again. **It fails closed:** a reader that cannot be confined reads nothing,
  and the listener hears no machine nearby (the link without discovery).
- **USB-D1, a device plugged in is let in by a rule (attacker 5,
  `moonwater usb`, src/sh/host.c).** The kernel holds the switches and the
  policy turns them: each device's and each interface's `authorized`, each
  root hub's `authorized_default` and `interface_authorized_default`, and
  usbcore's `authorized_default` for a controller that arrives later.
  `usb lock` keeps every device plugged in at the time as an allow rule
  (`VID:PID/SERIAL`; `VID:PID/-#HASH`, the serial's xxh64, for one too long
  to keep; `VID:PID/-@PLACE` for one with no serial, the place being the
  root hub's serial (its controller's bus address), the root hub's product
  and the port path, which a boot that numbers the buses in another order
  does not move; a `-@1-4` pin of an older policy is read by its bus port
  until `usb lock` writes it again) with the classes of the interfaces it
  has then, denies every
  class, and puts it in force: a new device is then never configured, so no
  driver binds to it. Locked, the interfaces of a device that is configured
  wait at 0 until each is judged, and are authorized and probed only for a
  class its rule keeps or a class allowed (`usb class storage allow`, which
  also has a new device configured so its interfaces can be judged): a stick
  that comes back as a keyboard as well has the keyboard held. A device the
  kernel holds unconfigured when the lock is taken (the command line holds
  every one at boot) is configured first, with every root hub's interfaces
  waiting, and kept as the classes it then shows; one that cannot be
  configured or shows no interface is not kept. A rule for a device named by
  its whole id while unplugged lets it in whole. A deny rule holds its device
  locked or not. One function judges
  (`usb_judge`) and one applies (`usb_apply`); boot, every verb and the
  watcher (a process of its own while the policy holds anything, woken by
  uevents it never reads and walking sysfs again every ten seconds) call
  those two and nothing else. A device that is built in is exempt from the
  lock's default only when it and every hub above it say `removable` is
  `fixed`, since a hub's own descriptor says that of its ports and a hub
  somebody plugs in can call every port it has fixed. A lock from off never
  holds a device present when it runs, which is the one net against a machine
  left with no keyboard (a lock while locked keeps only what the policy lets
  work); apply has none on purpose, since a machine whose keyboard has gone
  would admit the next one plugged in. What a device says of itself
  (product, serial) is attacker input: it reaches the status, the policy and
  the append-only log (`/root/usb.log`, 0600, cut to its newer half past 64
  KiB) only escaped and bounded, a serial in one spelling everywhere that is
  printable ASCII, and a serial typed with a control byte is refused. **What
  it cannot hold:** the kernel reads a new device's device descriptor and
  strings before anything decides (the descriptor parsers are the kernel's
  attack surface either way), a hub that is let in enumerates what is behind
  it, a device that copies the VID:PID, serial and classes of an allowed one
  is that device to the policy, DMA over Thunderbolt or PCIe is the IOMMU's
  (`sec_dma`), and root can undo all of it. The policy is read once `/root`
  is the kept one, so a device plugged in during boot is up until the first
  restore deauthorizes it: only `usbcore.authorized_default=2` on the kernel
  command line, which `sec_host_locked` sets, holds every device the firmware
  does not declare internal from the first moment. With the policy off, a
  deny rule is reactive (the watcher deauthorizes the device once it has
  enumerated). The tight host tier (`MOONWATER_STRICT_HOST` 2 and 3) locks a
  `/root` that keeps no `/root/usb.policy` (whether a watcher runs does not
  decide it: a disk taken after an ask's session had locked was left with no
  policy), at its first boot once USB has settled (the devices unchanged for
  two seconds after three of uptime, fifteen at most, in a process of its own
  so the boot waits for none of it), and at once, carried over the session's
  lock, when a disk is taken later in the boot: a lock taken at the first
  restore held a guest's tablet and keyboard, which had not enumerated yet.
  Whatever is plugged in by then is trusted, but a device first seen in the
  last two seconds of the settle (one plugging and unplugging to hold it to
  the cap) gets no rule. A lock that cannot be written to `/root` (or
  finds there something it cannot read, which is not written over, at boot
  or when asked for) is in force from `/run` until the machine restarts or a verb keeps one, on
  every tier, and `usb` says why. A verb waits for the usb lock 20 seconds at
  most and changes nothing if it cannot have it. The default tier changes
  nothing until the owner locks. `sec_locked` reaches `STRICT_HOST` 3
  through the whole build's level and does not compose `sec_host_locked`, so
  its image has the lock at first boot but not the command line unless
  `sec_host_locked` is named.
- **UI-D1, the compositor's window table is root's, not anyone's.** The
  compositor shares its state as one writable mapping (`/run/moonwater/windows`,
  window.c `struct windows`): every window's pixel buffers and input ring in one
  place, which every window program maps and reads and writes its row of. That
  is a trust boundary, not a public interface -- a program that maps it reads the
  Canvas terminal's picture (a root shell's) and writes keystrokes into the
  focused window's ring (a confused deputy over root). Every legitimate
  window program is root (the compositor and the terminal, log, launcher and
  settings the machine script starts; a KDE or Chrome client speaks Wayland/X,
  not this table), so the table, its memfd and its door FIFOs are `0600`
  (`WINDOWS_MODE`) and `/dev/spark`'s `WINDOWS_IOCTL_TABLE_GET` needs
  `CAP_SYS_ADMIN`, matching its `SET` side; a process that is only root in its
  own user namespace (a bowl under `unshare -r`) is refused. Isolated
  unprivileged windows are not supported: a single shared writable mapping
  cannot keep one client out of another's pixels or input, and giving them each
  their own would be a per-client protocol, not this table (MW-2026-050; the
  settings window builds its `moonwater` commands as an argv, never a shell line,
  and the Canvas terminal answers DA, DSR, DECRQM and XTWINOPS with fixed or
  numeric text only, never the title or any attacker string).

## Security tiers

Five profiles in `kernel/profile/` choose how the build leans where safety and
the reference disagree, and how hard the kernel under it is:

| Profile | Tools and builtins | `MOONWATER_STRICT` | The kernel |
| --- | --- | --- | --- |
| `sec_reference` | all | 0: the reference exactly, holes and all | the baseline |
| `sec_default` | all | 1: sanitise only what hostile input made dangerous | the baseline |
| `sec_workstation` | all | 1 | hardened in every region that costs a program nothing it expects: not lockdown, not the 32-bit ABI |
| `sec_hardened` | all | 2: refuse where the default sanitises | every region hardened |
| `sec_locked` | a kiosk's allow list | 3: and what init can shut until the next boot | every region locked |

`sec_default` is in the default profile list, and it is the only tier that
promises bash, dash and GNU behaviour for anything a script does. The others
are choices, and what they refuse is refused on purpose. All of them turn on
Yama, which Floodlight's restricted launches need. To pick one, name it last
after the rest of the default list:

```sh
sh build.sh debug_none limbo desktop wifi serial sec_hardened
```

**The baseline.** The kernel is built from `allnoconfig`, where an option
nobody names is off whatever Kconfig says its default is. Until `sec_baseline`
existed the shipped kernel had no stack protector, no usercopy bounds check, no
`FORTIFY_SOURCE`, no slab freelist hardening, no CPU vulnerability mitigations
on x86-64 (`CPU_MITIGATIONS` is a prompt that `allnoconfig` turns off, and
every mitigation under it goes with it), no `W^X` on riscv64 and a null-page
floor of 4 KiB. Every tier composes `sec_baseline` and its architecture's file
(`sec_baseline.x64`, `.arm`, `.riscv`); it costs close to nothing on the paths
this project measures, and the mitigations are chosen at boot from what the
processor says it is vulnerable to. A profile names its parts with
`#> includes NAME...` and an architecture's own lines sit in `NAME.ARCH`
beside it, which is how a region asks for x86's indirect branch tracking
without every other build reporting it dropped.

**The regions.** A tier is every region at one level; a region is a part of the
hardening that can move alone. Name its profile after the tier to take that one
further (`sh build.sh ... sec_default sec_net_locked sec_mem_hardened`), or keep
one at a gentler level than the tier around it by composing its profile first.
Each region has a hardened and a locked level, and the locked one is the
hardened one and more, so name one or the other. The files are
`sec_<region>_hardened` and `sec_<region>_locked`; `sec_exec` and `sec_build`
have only `sec_exec_hardened` and `sec_build_hardened`.

| Region | Hardened | Locked adds |
| --- | --- | --- |
| `sec_mem` (kernel) | heap zeroed on allocation, locals zeroed, caches never merged, typed buckets, shuffled free lists, random stack offset per syscall, checked lists, W+X mappings warned about at boot | zeroed on free and every call-used register cleared at return, partitioned kmalloc caches, corruption and an oops panic (and reboot after 5 s), KFENCE |
| `sec_cpu` (kernel) | indirect branch tracking, call depth tracking, straight-line speculation barriers, user shadow stacks, TSX off; arm64 memory tagging | `mitigations=auto,nosmt pti=on` |
| `sec_surface` (kernel) | no ACPI table or SSDT replacement from userspace, no MSR/cpuid devices, no ioperm/iopl, no vsyscall | no 32-bit or x32 ABI, no LDT, no FUSE, no binfmt_misc, no watch queue, no POSIX message queues, no io_uring or file handle calls |
| `sec_dma` (kernel) | VT-d, AMD-Vi and SMMU, strict translated DMA, no passthrough, firmware PCI DMA off | no PCI hotplug, USB4, FireWire or PC card |
| `sec_lockdown` (kernel) | integrity lockdown from boot, signed modules only, Landlock | confidentiality lockdown |
| `sec_files` | `STRICT_FILES` 2 and find without `-delete` or `-fprint*` | 3 |
| `sec_text` | `STRICT_TEXT` 2 | 3 |
| `sec_net` | `STRICT_NET` 2 and no `wget` or `fetch`; wifi with protected management frames always, and WPA2 only for a network saved with `wpa2`; time from NTS only and names over DNS over TLS only, unless `/root/ntp.nts` or `/root/dns.tls` says otherwise; a public certificate has to be logged (Certificate Transparency); a closed port off loopback is silent, no RST or ICMP unreachable (`net.ipv4.moonwater_silent`, FW-D1) | 3 |
| `sec_host` | `STRICT_HOST` 2: a disk is asked about, `kptr_restrict` 2, io_uring refused, Yama 2, widest mmap randomisation, USB locked at the first boot (USB-D1) | 3: no module loads, no ptrace attach (Yama 3), `hidepid` on /proc, the data partition `nosuid`, `/dev/shm` `noexec`, `usbcore.authorized_default=2` on the command line |
| `sec_bowl` | `STRICT_BOWL` 2 | 3 |
| `sec_exec` | Floodlight sealed with every command-making switch off | (none) |
| `sec_build` | the image's own programs built with zeroed locals, probed stack frames, cleared call-used registers and the stack protector (+5.7% on a shell start, inside the noise on a compute loop) | (none) |

The userspace regions are what `MOONWATER_STRICT_FILES`, `_TEXT`, `_NET`, `_HOST`
and `_BOWL` set (`-1` follows the whole build's level, so a region left alone
changes nothing), and a tool names its region and never the whole level. The
sysctls init writes before it starts anything are the FILES and HOST regions'
(`fs.protected_*`, `kptr_restrict`, `dmesg_restrict`, `io_uring_disabled`,
`randomize_va_space`, at the tight level Yama and mmap's randomisation, at
the locked level Yama 3 and `modules_disabled`); they used to be written by the network
watcher a moment after boot, and not at all where it never started.
`/proc` and `/sys` are mounted `nosuid,nodev,noexec` and `/dev`
`nosuid,noexec` by the kernel module, at every tier.

What a tier's image is expected to say once it has booted is a table in
`lane_tier` (`test/run`), and `MOONWATER_IMAGE=dist/bootx64.efi
MOONWATER_TIER=hardened sh test/run tier` reads it back from a guest: the
sysctls init wrote, the mount flags, the lockdown line, the W+X page-table
check and the IOMMU's strict mode. `sh test/run switches` is the composition
half (what each tier and region names, and that a region keeps to its own
level).

What `sec_hardened` (`STRICT_TIGHT`) refuses where the default does what GNU
coreutils does:

| Tool | Default (GNU) | `sec_hardened` |
| --- | --- | --- |
| `mkdir`, `mkfifo`, `mknod`, `install`, `cp` | make an entry in a world-writable directory without the sticky bit | refuse it (`Permission denied`) |
| `cp` | writes through a destination that is a link; under `POSIXLY_CORRECT` makes the file a dangling link names | replaces the link itself, never touching what it points at, and always refuses a dangling one |
| `split`, `csplit`, and any output opened for writing (`tar -f`, `wget -O`) | write through an output name that is a link | replace the link with the new file |
| `chown -R -L`, `chgrp -R -L` | follow the links met in the tree | follow none below the operand |
| `cp -r` | makes the new directory where it goes and fills it | builds it in a private stage and publishes it whole |
| `install` | leaves the copy in place, mode 0600, when the owner asked for is refused | takes the copy away and says it was not published |
| `pinky -l` | copies each user's `~/.project` and `~/.plan` to the terminal as they are | leaves them out |
| `wget`, `fetch` and every other HTTP client in the tree | accept a `204` that declares `Content-Length` or `Transfer-Encoding` (its body is never read; the connection closes after the one response), read a `205`'s content like any other body, and take a `Content-Length` that repeats one number (twice, or `3, 3`), a folded line after an ordinary field and any byte but NUL and a bare CR in the reason phrase, as wget and curl do | refuse a `204` that declares any body framing, a `205` with a non-zero length, chunked or close-delimited content (RFC 9110 15.3.5, 15.3.6, RFC 9112 6.1), a repeated or listed `Content-Length`, any folded line and a control byte in the reason phrase |

Every row but the HTTP one has a check in `floodlight_hardened` (`test/run`),
run against a shell built from `kernel/profile/sec_hardened`'s configuration;
the default half is the differential engine's, against GNU. The HTTP row's
tight half is `CHECK_net` built with `MOONWATER_STRICT` 2 (the `net-tight`
tally of `sh test/run net`) and the framing harness's second tier; its default
half is held against GNU wget and curl by `wget_mutation`.

Two differences are not tier choices. `kill` is util-linux's, not
coreutils' (`kill -l` takes several names, `-s0` is a signal), because
coreutils 9.11 does not build `kill` by default. `install` invoked as
`ginstall` reports itself as `install`.

Locales are read from the machine, not compiled in. Where `/usr/lib/locale`
(or its `locale-archive`) has a locale's files, `sort`, `ls`, `join`, `comm`,
`expr` and the shell's globs and `[[ a < b ]]` order text by that locale's
`LC_COLLATE` exactly as the C library's `strcoll` does, `sort -n -h -g` read the
locale's decimal point and thousands separator, and `date`, `numfmt` and friends
read `LC_TIME` and `LC_NUMERIC`. The image ships no locale data, so there every
locale is C and orders by byte; the C locale never pays for any of this. `uname`
answers `Moonwater` for `-o` and ends `-a` at the machine: Moonwater is not GNU
and does not say it is.

To take a single tool or builtin out, add a line to a profile of your own, or
use menuconfig:

```sh
# CONFIG_MOONWATER_TOOL_WGET is not set
# CONFIG_MOONWATER_BUILTIN_HISTORY is not set
```

`./build config-header <profile> <header>` shows what a profile turns off, and
`./build surface <profile>` lists the names it would link.

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
| DNS | `dns_oracle`: `dns_reply_result` against an independent reading (RFC 1035 and the resolver's contract: identity, header, every section framed, compression ceilings and jump budgets, the CNAME chain) over 20,000 replies from a grammar, a fifth damaged, every status reached; exact question, ID, peer; per-query source port, 0x20, EDNS0 with fallbacks; own resolver first; `DNS_DISCARD_MAX`; `dns_fuzz`, `sh test/run net`; a validated TC reply keeps the full query deadline for TCP, while only the UDP EDNS trial is capped at one second; DNS over TLS as a setting (`dns_tls`: 18 rows of setting, leaf, port, clock and tier against a resolver on 1.1.1.1 and the network's own on 10.0.0.1 in a namespace; the setting and the four resolvers in CHECK_net at both tiers) | with DNS over TLS off (the default tier's default) an on-path peer can still forge an answer, and with `on` an attacker who blocks 853 gets the plain query; NTS, SNTP, hostid, logger, waterlink and caIssuers names are always plain |
| DHCPv4 | xid, MAC, server and OFFER-peer binding; option overload, END and zero padding; lease sanity; ARP probes before install, and a DHCPDECLINE (RFC 2131 4.4.1) with a ten-second hold-off for an address another station answers for; watcher cut once per link news, never between a REQUEST and its ACK window's end (DHCP-D3); replies off a packet socket judged as IP and UDP judge them (DHCP-D1): `dhcp_frame_fuzz` against an independent RFC 791/768 reading and a classic BPF interpreter running the kernel's prefilter, the CBMC proof `dhcp_frame`, `CHECK_net`'s rows and its loopback socket in namespaces of its own, `net_parser_work_states` off both sockets; netem scenes at `rp_filter` 1 and 2, a server that unicasts its replies, and link news after a REQUEST with an ACK 0.6 s late or never (one DHCPRELEASE); the confinement refusing where seccomp answers ENOSYS (DHCP-D2); route ownership and rollback fault matrix (`net_lease_transitions`); `dhcp_fuzz`, `sh test/run netem net machine` | the DECLINE and the RELEASE are checked as built packets, through their confined child and against the scripted netem server, never against a production server (a server may ignore a RELEASE broadcast from 0.0.0.0); the frame judge's proof is bounded at 32 bytes (40 ran past an hour) and the fuzz is what covers longer frames; the loopback socket check is NOT RUN under qemu-user |
| SNTP, NTS | 64-bit nonce, peer, mode, stratum, timing, exactly 48 bytes at the tight tier; each sample its own wait; `sntp_fuzz`, `sntp_era` (2036, 2038, 2104), `sh test/run netem machine`; NTS (RFC 8915): the AES-SIV-CMAC-256 AEAD against RFC 5297 and OpenSSL (`crypto_vectors`, 41 vectors on three machines, constant instruction counts and data addresses for six secrets), 904 NTS replies built independently and refused for every changed byte and every cut, the key-establishment parser over 26 shapes and every cut length (CHECK_net), `nts_fuzz`, and the client against chrony 4.8's NTS server and against servers that are wrong (`nts`, 92 rows: key establishment counted, torn and linked state, every tampered, cut, replayed, stripped or dropped reply, NTSN, no ALPN, TLS 1.2, a wrong or expired certificate, noise, silence, a private time server, the floor's judgement, `only` and `off`, the tight tier's default); the floor and `clock.good` in `tls_chains`, `tls_dates` and `moonwater_cli` | `nts` needs a chronyd built with NTS (`MOONWATER_CHRONYD`) and is NOT RUN without one; with no NTS server reachable the default tier takes plain NTP between the floor and 2104 (a day once the clock is set, two seconds once synchronised) |
| HTTP, URL | `localhost` and `*.localhost` reach 127.0.0.1 without a resolver (RFC 6761; `wget_hostile` with a stub resolver, curl held to the same rows); host spellings named against GNU wget and curl; the scheme rule at both tiers (`http_urls`); sink-side validation of host, target, headers; userinfo, schemes, fragments refused; one framing reading; redirect and HTTPS-downgrade bounds; body deadline; per-hop host, address, SNI and Host identity; `http_response_framing` (with `http.client` and curl), `wget_mutation` and `wget_hostile` (GNU wget and curl as live oracles), `https_downgrade`, `http_fuzz` | the first address a name resolves to is the one connected to (no multi-address fallback, which neither identity nor the tight tier's rebinding rule would survive unexamined); interface scope is not a part of an address here |
| TLS, X.509 | transcript, Finished, AEAD, record sequence, state ordering; strict DER; CertificateEntry extensions refused but a staple the hello asked for; delivery schedules and FIN/RST cuts (`tls_peer --schedule`); SAN and name constraints, key usage, EKU, dates; chains against OpenSSL and Go `crypto/x509`; record nonce coverage over every value in every sequence byte lane and guarded unaligned buffers; record-header coverage over every 16-bit length and 8-bit type; `tls_chains` (also wget's status and words for each refusal), `tls_hostnames`, `tls_dates`, `public_suffixes`, `anchors`, `tls_peer`, `tls_der_fuzz`, `tls_hs_fuzz`, `tls_verify_fuzz`; `x509_corpus` by hand over 643 hosts | name constraints in more shapes |
| Revocation | `tls_chains` serves CRLs made by `openssl ca -gencrl` (listing the leaf or not, partitioned by `issuingDistributionPoint`, another partition, CA-only, past `nextUpdate`, another key under the issuer's name, absent, PEM, an unknown critical extension, a delta, an RSA issuer, an intermediate and the first CA revoked) and holds wget to `openssl verify -crl_check` / `-crl_check_all`, at the default tier and again at the tight one, words and status included; `tls_peer` staples `openssl ocsp` responses (good, revoked, unknown, stale, no `nextUpdate`, another certificate's, a broken signature, a delegated responder with and without `id-kp-OCSPSigning`, `tryLater`, must-staple) over scripted TLS 1.3 and 1.2 flights at both tiers against `openssl ocsp -respin`, the wire shapes a staple may not take, and `openssl s_server -status_file` over both versions; `CHECK_net` (signed fixtures, the tiers at both `MOONWATER_STRICT` builds, the five-minute skew to the second, the TLS 1.2 relay, the ClientHello and ServerHello extension, every ceiling exact and one over); `tls_ocsp_fuzz`, `tls_crl_fuzz`; `net_bounded_proof` and `net_exhaustive_proof` below | the CRL cache lives for one process, so each `wget` run fetches again; a CRL over `TLS_CRL_MAX` is no answer |
| Certificate Transparency | `tls_ct`: 23 leaves under the harness's own six logs (five operators, one retired a month ago, one whose interval holds no leaf here) against a model of RFC 6962 and Chrome's policy -- two operators, one log not retired, two or three SCTs by lifetime (90, 180, 400 days), a broken signature, an hour ahead, an unknown log, retired before and after its SCT, the interval, version 2, SHA-384, the list between two extensions, a TBS whose DER length shrinks without its list, another issuer's key, a byte after the list, no list, SCT extensions signed, sixteen broken before two good -- with the harness's precertificate held to `cryptography`'s first; end to end through wget at the tight tier (logged, and refused in its words with status 5) and the default (either taken); `CHECK_net` on 1.1.1.1's real leaf (three SCTs from DigiCert, Sectigo and Google logs; one broken is too few for 355 days; another issuer's key holds none) and every log id the SHA-256 of its key; `ct_logs` regenerates the table byte for byte from the list its header names; live at the tight tier against Google, Cloudflare, Let's Encrypt, DigiCert, Sectigo, Amazon, Apple, GitHub, Wikipedia, gov.uk and bund.de, and DNS over TLS to 1.1.1.1, by hand | SCTs delivered in TLS or a staple are not read; the log table is a snapshot of Chrome's list |
| Confinement | `CHECK_net` on all three machines: `net_apart_filter_decisions` runs the built filter in a classic-BPF interpreter over the calls a fetch makes and the ones it must not (another architecture, x32, unix, netlink, packet, IPv6 and raw sockets, a named protocol, options fetching never sets, executable memory, openat, execve, ptrace, mprotect, ioctl, kill, prctl, read); natively, `net_apart_confinement` (the child keeps its pipe, output and standard error and nothing else, every call fetching needs works, and openat, execve, ptrace, the five refused socket kinds, executable memory, mprotect and ioctl each die of SIGSYS after it answered) and `net_apart_fetching` (fetch's and wget's manners through the child against a loopback server that reads `Seccomp: 2` and `NoNewPrivs: 1` off the waiting child; a planted openat after a working fetch dies; a 12 MiB body grows its store inside the filter; the resolver answers from the servers read before the filter); under qemu-user the refusal is checked, and the deaths are checked on arm64 and riscv64 themselves by `guest_confinement` (the boot lane with `MOONWATER_IMAGE=dist/bootaa64.efi` or `bootriscv64.efi`: CHECK_net's three confinement groups and CHECK_bowl, the Wi-Fi station's confinement with it, run as root in the machine's own guest, 114 and 1,480 checks with no failure on both); `security_hygiene` fails any call into `http_run`, `http_spider`, `http_link_open`, `http_stream_open`, `tls_connect`, `tls_connect_until`, `tls_plain_get`, `tls_aia_fetch`, `tls_crl_fetch`, `nts_ke`, `dns_query_tls`, `dns_resolve_wanted` or `locale_auto_ask` outside a task handed to `net_apart`, a task called directly, and a filter install that takes anything but success; `wget_mutation`, `wget_hostile` (its address rows under `unshare -Urnm`), `tls_peer`, `https_downgrade` and the misc lane's wget and fetch rows run through it | the drop to nobody as root is seen by hand (`perf trace`), not by a lane; the child may connect to any IPv4 address its user can reach, so the tight tier's private-address rule and caIssuers' public-only rule hold only while the child is not compromised |
| Crypto | OpenSSL vectors and every `lib.c` architecture body: `crypto_vectors` (`--wycheproof DIR` by hand; AES-SIV-CMAC-256 against RFC 5297's own bytes), `crypto_fuzz` | none known |
| Wi-Fi | RSN, EAPOL-Key, replay counters, source address, scan parsing; all four GTK slots and both IGTK slots, zero-valued first keys, driver refusal/retry and roam reset, message 3's elements held to the beacon's (`wifi_key_state`); the strongest 64 names kept and the associated row never evicted; one BSS donates every field of a row; the RSN offer of both frames against a model, a twin offering less ranked last (`wifi_scan_fuzz`); PSK, PSK-SHA256 with protection and SAE key frames (HMAC-SHA1 and AES-CMAC MICs, descriptor versions 2, 3 and 0, IGTK KDEs, elements bent or stripped in message 3) under `wifi_eapol_fuzz`; the SAE exchange's frame reader, state and bounds and the budget against a model (`wifi_sae_fuzz`); SAE (hunt and hash-to-element), the KDF PTK and CMAC against a Python reference and RFC 4493 (`CHECK_waterlink_service`); `wifi_join_plan` at every tier over every offer, rule and secret (`CHECK_bowl`, again in its tight build); `wifi_air` (hwsim: the pmf and sae families, spoofed deauthentication, hostapd 2.11 with each password element method and a token, every family through the confined station); the station's filter in a classic-BPF interpreter over 48 calls and its confinement natively in a user namespace of its own (its sockets come back, openat, an IPv4 or raw packet socket, execve, ptrace and prctl die of SIGSYS) and refused under qemu-user (`CHECK_bowl` on three machines and its tight build); `security_hygiene` holds the join and the keeper to the station, the station to its broker and the broker to a join apart | a forger outranking every retained row; a second physical chipset; the first join of a network is trust on first use |
| Waterlink | Noise handshake, cookie, replay window, grants, revocation; mDNS reads Internet class only, RCODE zero, TTL nonzero, an SRV with a non-root target and a nonzero port, exposes only a PTR and SRV intersection, and shares a packet-sized compression-pointer budget across every name; the reply and answer budgets move only after a successful send; `IP_MULTICAST_IF` failure refuses the send; a source holds at most `LINK_GREET_SOURCE` of the 16 greeting slots; legacy verifier migration is all-write plus `fsync`, and until it succeeds the listener stays off while commands keep the groups (their save is the scrub); every elapsed time is ordered first; `waterlink_fuzz` rejects a wake/fill cycle that remains immediately due without output; `sh test/run waterlink link`, `waterlink_sanitized`, `waterlink_fuzz` (MSan with `MOONWATER_MSAN=1`); mDNS read in a confined reader: its filter in the classic-BPF interpreter, what the listener believes of a reading (every count at its room, one over refused), and an announcement and noise read apart against the reading made in place (`CHECK_waterlink_service`, natively in its user namespace) | handshake flood under netem; address rotation reaches the global greeting ceiling |
| Saved state | wifi and bluetooth lists and the wifi rules written beside themselves and renamed, opened nonblocking, regular files only, the rules 0600 and kept to the saved networks; no hash of a group secret on disk; `sh test/run cli link` | |
| Secrets, boot choice | `history_secret_line` rows including what the denylist does not see (`CHECK_bowl`); `link group NAME -` and `wifi add` at a terminal (`moonwater_cli`, `link`); the tight tier's refusal of both argument forms (`CHECK_bowl` again at `MOONWATER_STRICT` 2); `host_link_external`, `host_disk_external` failing closed and `host_lone_taken` (`CHECK_bowl`); a fixed NVMe install still taken and another build still asked about (`install` lane) | a USB install in a guest (found only when it enumerates before the census, racy in QEMU); the `removable` files of a real PCI port need hardware |
| USB devices (USB-D1) | `moonwater_cli` over a sysfs of files: what lock keeps (classes, places, a long serial's hash, nothing fixed, a device behind an unfixed hub not fixed), a held device configured and kept as its classes, held, allow, deny, ambiguity, class, a known stick grown a keyboard held, an old `-@PORT` pin read and rewritten, places across renumbered buses, a no-serial clone of a hashed serial held, a lock that cannot be written, or a policy that cannot be read and is not written over, in force from `/run`, a bounded wait for a held lock, a failing default logged, unlock with the deny kept, the file's grammar and its 64-rule cap, the watcher alone and ending, hostile product and serial spelled with no control byte anywhere (`cli` lane); the `usb` lane against a booted kernel with QMP hot-plug: a stick held with no SCSI host or disk, a keyboard with no input device, allow PORT, class storage, the deny across unlock and again at a replug, hostile serials, the policy read at a second boot from an install (33 rows, 23 red on an image without it), and with `MOONWATER_USB_TIER` a `sec_host_locked` image's first boot: every device held from the start, the lock taken once USB has settled keeping the tablet and the keyboard as hid, a later stick held, then an install of the tier booted not from its disk so that it asks, the disk (left with no policy) taken after the session's lock and locked, usb saying lock with the watcher running and the kernel at 0 0 0 (13 rows; 46 of 46 on images built with 4db51897, 42 of 46 on images from before this review's fixes: the long serial's hash, the hid rules and two of the ask rows) | a real port's `removable` and ACPI's hard-wired ports (QEMU says unknown for every port, so what `authorized_default=2` admits as internal and the fixed exemption are not proved in a guest); hostile descriptors (fuzzed apart); the boot window without the command line word; Thunderbolt and other buses |
| Terminal, JSON reader | `term_fuzz` (grid, write cuts, resizes, keys, pointer between writes; the cursor stays on the grid; it fails on a planted clamp bug) and `bowl_json_fuzz`, ASan/UBSan, smoke in `term` and `bowl` | KERNEL_MODE emulator build, MSan |
| Shell, OS boundary | generated Bash and Dash differential; private edit files; `edit` draws a file's controls, DEL and invalid UTF-8 as `?` and drops C1 controls spelled in UTF-8, in rows and status line (`edit` lane, `hostile`); PTY setup; tar pinned parents; hostile environment and privilege matrices; `pathname_race`; effect-based coreutils (`sh test/run shell builtins files tar`) | |
| Faults, resources | seccomp entropy failure; partial I/O, EINTR, ENOSPC, deadline and clock-jump faults; TLS 1.2/1.3 handshake writes and KeyUpdate replies share the operation deadline, including buffered writev batching; deferred read budgets are clamped to the enclosing body deadline; buffered payload cannot escape that body deadline; ordinary HTTP/TLS writes use one bounded nonblocking slow path, and queued whole writes need no clock or poll; interrupted output writev resumes its spans under one absolute retry budget, without a clock on uninterrupted writes; fatal TLS protocol errors spend both keys and cannot publish later buffered data (unexpected record types swept over both versions and sequence boundaries); descriptor and mmap exhaustion; once-armed `memory_reserve`, writev and socket faults mid-path; namespaces with netem (`sh test/run netem`) | SNTP allocation faults; a writev syscall that itself blocks on a disk or pipe is outside the interruption budget |
| Kernel (ring 0) | `core_state`, `pane_pages`, `shared_page`, `console_queue`, `term_streams` (kit lane); `ring0_hostile` on KASAN, UBSAN, lockdep or KCSAN images (`MOONWATER_IMAGE=dist/bootx64.efi sh test/run ring0`); image defaults for redirects, router advertisements, RFC 1337, SYN cookies, `rp_filter` and the ARP settings (`sh test/run boot`, `net_sysctl`) | the `rp_filter` and ARP row of the boot lane has not run on an image yet |
| Silent closed port (FW-D1) | `mw_net_silent_gate` (`kernel/kernel.c`) held to its rule in C on x86_64, arm64 and riscv64, over random device flags, a NULL device and seven switch values with the device unmapped while the switch is off (`kernel_ports`, kit lane); the watcher writing the switch 1 at `STRICT_NET` 2 and 3 and leaving it at the default, over a scratch `/proc/sys` and with the file absent (`net_sysctl`); two guests on a private link, a closed TCP and UDP port refused and the RST and unreachable counters moving at 0, dropped with no counter at 1 while loopback (v4 and v6), the target's own address and an open listener still answer, back at 0, IPv6 over link-local, a tight tier booting at 1 (`sh test/run firewall`, 29 rows, 11 red on an image without the guard edits) | the flood A/B measures the gate's CPU saving, not a line-rate generator or a distributed flood; the tight-tier boot row is NOT RUN without `MOONWATER_NET_TIER`; the open-port, forwarded-traffic and other-ICMP paths are not driven for CPU; arm64 and riscv64 are held by the qemu-user differential and both assemblers, not by a boot; the timing of a dropped reset is a side channel this does not close |
| Supply chain | bowl bootstraps pinned by digest or signing key, refusal on any mismatch; a download held to a kernel size ceiling (2 x the measured size + 64 MiB, `RLIMIT_FSIZE`); the signature reader under `bowl_sig_fuzz`; `sh test/run bowl` | |

Revocation by hand, 2026-10-09: wget at both tiers, with the shipped anchors,
against 30 public hosts across Let's Encrypt, Google Trust Services, DigiCert
(Global G2 and G3, EV), Sectigo, GlobalSign, Amazon, Microsoft, Apple,
SSL.com, HydrantID and Thawte, 22 of them stapling: no TLS refusal at either
tier (the tight tier first refused bing.com and learn.microsoft.com, whose
first CRL mirror answers 403; mirrors are now tried in turn). Over the 642
live chains of `x509_corpus` the 1,872 certificates parse the same before and
after (1,854 parse; 1,708 name an `http:` CRL; none asks for a staple). Cost,
median of seven interleaved runs, parent tree against this one: +17 ms for a
stapled DigiCert G2 host, +49 ms for a Let's Encrypt leaf (a 61 KB CRL),
+66 ms for Netflix (125 KB), +111 ms for SSL.com (1.4 MB); on loopback a
leaf's CRL adds 0.35 ms of wall and 0.15 ms of CPU to a 0.75 ms fetch, three
CRLs 0.82 ms and 0.40 ms, and a stapled answer 0.11 ms and 0.10 ms.

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
repeating the final event to include persistent junk, read off the client
socket and again off the packet socket (4,259,840 in all), where the refused
reply is a whole ACK sent to another station's address. Completion and receive
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
for every lifetime; `dhcp_walk` against an RFC 2132/3396 walker for every
region of up to ten bytes over the bytes it tells apart, under ASan;
`tls_der_integer` and `tls_der_named_bits` on every content of up to four bytes
against X.690's INTEGER and named-bit-list rules (4,311,810,305 cases); and
`tls_date_seconds` and `tls_revocation_fresh` on every day from 1950 through
2399 at four times of day against days counted one by one. Each
reports how many inputs were accepted and fails if either answer never occurs.
`net_bounded_proof` has CBMC prove memory safety, no pointer or signed
overflow, termination by unwinding assertion and a stated property for every
input up to a bound: `byte_reader` for buffers of any length and `byte_store`
for any append (every parser reads and writes through these), `dns_copy_name`
(7 bytes), the ASN.1 helpers (8), netlink attribute finding (24),
`http_header_end` with `http_response_fields` (10), the chunk-size and trailer
lines (10), `http_split_into` (10), `http_path_simplify` (8: no dot segment
is left and a second pass changes nothing), the DHCP frame judge (`dhcp_frame`:
`dhcp_unframe` agrees with RFC 791 and 768 read by index on the verdict, the
addresses, the port and the message's place for every frame of up to 32
bytes, and `dhcp_frame_control`: `dhcp_frame_vouched` agrees with cmsg(3)
over every control buffer of up to 48; `dhcp_frame_filter`: every frame of up to 68 bytes, so every
header length, that the judge accepts with its UDP checksum vouched for -- the
most it accepts -- is kept by the kernel's prefilter, run as classic BPF from
its own bytes), and the revocation parsers' small pieces: the DER cursor
`tls_der_value` with `tls_der_integer` and `tls_der_named_bits` (8: a value
inside its reader past its header, the reader resuming after it, DER's one
spelling), `tls_entry_status` (20: a staple only when asked, alone, inside the
extensions), the TLS Feature parser (12: must-staple only with an INTEGER 5 in
it), `authorityKeyIdentifier` (12: the key inside, no issuer without its
serial) and `issuingDistributionPoint` (12: at most one of the three
only-flags, names inside and never relative). `tls_constraints` and
`tls_key_usage` hold basicConstraints (15 bytes) and keyUsage (6) equal to an
independent RFC 5280 reading. Every proof is run again with its
outcome marked, so an outcome no input reaches fails as vacuous. The lib.c
routines the parsers call stand in as the contracts written in the harness.
Not proved: the X.509 parser and chain builder as a whole, the whole OCSP
response and CRL parsers (fuzzed, `tls_ocsp_fuzz` and `tls_crl_fuzz`), the TLS
handshake state machine, `http_run`, DNS answer selection and the DHCP client's
reader beyond its frame judge and option walk; those rest on fuzzing,
differential oracles and the mutation lane.

## Ceilings

Exact-limit and one-over rows live in `CHECK_net` (`test/checks.c`, with the
ledger at the top of its section).

| Parser | Ceiling |
| --- | --- |
| DNS | message `DNS_MAX_MESSAGE`; label 63, name 255; `DNS_CNAME_HOPS` 16; `DNS_WORK_POINTERS` 4,096 compression jumps per framing section and across answer-selection passes, at most 16,384 per reply; `DNS_DISCARD_MAX` 64; `DNS_SERVERS_MAX` 3 |
| DHCP | `NETWORK_DISCARD_MAX` 64 rejected datagrams per OFFER or ACK phase, shared across packet syntax/peer and lease-state rejection, and off the packet socket across frames refused for their kind, their IP or UDP headers or their destination; `DHCP_FRAME_ROOM` 1,536 bytes a frame and caller room for its message; `NET_DHCP_ACK_GRACE_SECONDS` 2 past link news or the budget for a REQUEST in its ACK window, once per exchange |
| Netlink | `NETLINK_DISCARD_MAX` 64 unrelated datagrams per transaction; 16 MiB datagram |
| SNTP | `SNTP_DISCARD_MAX` 64 non-terminal receives per exchange, an NTS reply that does not authenticate counted among them |
| NTS | `NTS_KE_ROOM` 4,096 bytes of key-establishment reply in `NTS_KE_SECONDS` (6 s for connection, handshake and reply); `NTS_COOKIES` 8 of at most `NTS_COOKIE_MAX` 256 bytes, a longer cookie passed over; `NTS_SERVER_MAX` 64 bytes of name; `NTS_PACKET_ROOM` 1,280 bytes a packet; four key-establishment servers a walk |
| DNS over TLS | `DNS_MAX_MESSAGE` a reply; `DNS_TLS_SECONDS` 2 s for every TLS try before the UDP fallback (`on`), the lookup's own 3 s with `only` |
| TLS | `TLS_RECORD_MAX`, `TLS_HS_MAX`, 64 extensions, 8 certificates |
| Revocation | `TLS_OCSP_MAX` 16,384 bytes a staple, `TLS_OCSP_RESPONSES` 16 SingleResponses, `TLS_OCSP_CERTS` 4 certificates a response; `TLS_LIST_EXTENSIONS` 16 extensions a CRL, CRL entry, response or single response; `TLS_CRL_ENTRIES` 524,288 revoked certificates a CRL (1.7 times the 311,758 of the largest public CRL seen, DigiCert Global G2 TLS RSA SHA256 2020 CA1 at 11.7 MB on 2026-10-09; Let's Encrypt's shards are a few hundred bytes); `TLS_CRL_MAX` 16 MiB a CRL, which is also all three DER length octets can say; `TLS_REVOCATION_SECONDS` 10 for every fetch of one handshake together, inside the handshake's 30 (each name lookup its own 3); `TLS_CRL_POINTS` 4 URIs tried a distribution point; `TLS_CRL_HELD` 8 CRLs and `TLS_CRL_HELD_BYTES` 32 MiB held per process, least recently used out first; `TLS_CRL_RETRY` 300 s before a failed fetch is tried again; `TLS_REVOCATION_SKEW` 300 s either way of `thisUpdate` and `nextUpdate`; 16 names a side compared between a distribution point and an `issuingDistributionPoint` |
| HTTP | `HTTP_URL_MAX`, `HTTP_HEAD_MAX`, `HTTP_FETCH_MAX` (16 MiB in memory), `HTTP_HOPS`, `HTTP_WRITE_SPANS`; `HTTP_BODY_SECONDS` (300 s) bounds a stored body in every tier and a streamed one at the tight tier, where the default keeps GNU wget's idle timer alone |
| Waterlink | 8 instances a packet; at most the packet's byte length in shared mDNS compression-pointer jumps; `LINK_GREET_SOURCE` 8 of 16 greeting slots; one reply and one answer an interval |
| Wi-Fi | SAE, cheap input to dear crypto: one peer commit taken in (two scalar multiplications) an exchange, after checks that cost nothing; one token round, `WIFI_SAE_TOKEN_MOST` 64 bytes; `WIFI_SAE_FRAMES_MOST` 64 authentication events read, `WIFI_SAE_SECONDS` 10 s; `WIFI_SAE_PEER_MOST` 6 exchanges with one access point and `WIFI_SAE_ALL_MOST` 24 with all in any `WIFI_SAE_WINDOW_MS` 60 s, by the boot clock in `/run/moonwater/wifi.sae`, one over refused before the work; 40 to `WIFI_SAE_PASSES_MOST` 255 hunting passes. EAPOL: a 512-byte frame, `CRYPTO_WRAP_MOST` of key data. Scan: `RADIO_AIR_MOST` 64 names; `RADIO_AVOID_MOST` 64 access points given up on, an hour each. `RADIO_RULES_ROOM` 4,096 bytes of rules, three a saved network |

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
architecture gate above.

## Open

- **Closed, unmerged work:** wget and curl parity of defaults (#22). Its
  branch is gone; the pull request keeps its head (`git fetch origin
  pull/22/head`). The clock floor (#17), NTS (#18) and DNS over TLS (#20) are
  in the tree; WPA3 and 802.11w (#19) are in, rewritten on this tree (WIFI-D1
  to D6). #21's packet socket, REQUEST-to-ACK hold, announcements and
  `rp_filter` are in (DHCP-D1 to D3); its conflict probe inside the child was
  not taken, the watcher's own probe and per-link DECLINE hold-off were there
  first.
- **Wi-Fi:** a network at 6 GHz alone is not scanned for by a join (its
  channels are listened to, which outlasts the scan's five seconds); WPA3's
  password identifiers, groups other than 19, SAE-PK, AKM 24 and 25 and FT are
  not built. The first join of a network is trust on first use (WIFI-D4), and
  a WPA2/WPA3 network never joined with WPA3 can be pushed to WPA2 at the
  default tier by an attacker who stops the exchange (a refusal or
  deauthentication forged before the confirm), unless its access point says
  Transition Disable for WPA3-Personal in an authenticated message 3, which
  is then kept as WPA3 joined with (`/root/wifi.rules`). EAPOL message 1 is unauthenticated by
  design; a roam the driver made on its own has no elements to hold the next
  message 3 to. SAE's commits are unauthenticated before the confirm, so a
  forger in range can make an exchange fail (the budget bounds what that
  costs, not that it happens). hwsim has no firmware and no real rekeys; a
  second physical chipset is unproved. The SAE budget's file being unwritable
  leaves exchanges counted per exchange only.
- **Time and names:** at the default tier, with no NTS server reachable, plain
  SNTP is believed between the clock's floor and 2104 (a day's step once the
  clock is set, two seconds once synchronised) and a forged step is undone by
  the next NTS answer; `clock.good` lags the truth by the time the machine was
  switched off; DNS over TLS is off, so names are as forgeable as the network
  is; the NTS-KE certificate of the first exchange of a boot is judged as of the
  floor, so a stolen key whose certificate expired after the floor can answer
  it; NTS's and SNTP's own names, and a caIssuers location, are asked in plain.
- **No change planned:** a response cut inside its status
  line or a header fails in both tiers (GNU wget and curl disagree with each
  other there); an endless close-delimited body at the default tier can fill the
  disk; the radio lock is held across a whole join; the elements a scan reads are
  unauthenticated, so the join orders by what the saved network asks for and has
  taught it, and the handshake's message 3 authenticates the elements the
  choice was made from.
- **Parsing still in the asking process:** every client that asks a server
  -- the HTTP/TLS clients, `host`, the NTS key establishment, every time
  query, and the name lookups of hostid, logger, a Waterlink service and
  NTS's time server -- runs apart (Decisions), and DHCP has its own confined
  child, and so do the Wi-Fi station (WIFI-D7) and the Waterlink listener's
  mDNS reader. What still reads bytes from the wire where it lives:
  Waterlink's datagrams, in its listener, and netlink from the kernel. A
  datagram is held to the keyed MAC of the listener's own public key
  (`waterlink_gate_passes`) before any of it is read, a handshake message is
  of one fixed size and layout, and what a linked machine sends after the
  handshake is the word of a machine that may run commands as root here by
  design (`waterlink_pre_fuzz`, `waterlink_fuzz`, `waterlink_noise`);
  netlink comes from the kernel (`netlink_fuzz`). Neither is a confined
  parser.
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
   *Checked 2026-10-11 for descriptors:* wget with standard input, output and
   error closed, or error alone, fetches whole (its confined child's answer
   pipe never lands on a descriptor the child also writes its words to).
6. **Entropy:** clone, restore and fork at each random acquisition; counter and
   key rollback; never repair repetition with public values.
   *Checked 2026-10-11 for fork:* every random value the network code uses
   -- TLS client random and ECDHE scalars, DNS ids and ports, DHCP and SNTP
   transaction tags, NTS identifiers and nonces, SAE and Waterlink
   ephemerals -- is a `getrandom` call at the moment of use
   (`system_random_fill`, `network_transaction_secure`); no userspace pool
   exists to be copied by the forks of `net_apart`, the Wi-Fi station or the
   mDNS reader, and libc's `rand` is used by none of them.
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
