# Incident response

This file holds two things: the steps we follow when a public disclosure puts
Moonwater's network stack, keys or trust data at risk, and the log of every
incident handled that way. Each log entry gives the dates, the commits that
fixed it, and the tests that fail if the fix is removed. Add an entry in the
same session as the fix, and put the commit hashes in it before the session ends.

Times are UTC unless a `+02:00` follows. Commit times are the author dates in
`git log`, which carry the committer's offset.

## Procedure

1. Find out whether the data is ours. Search `src/net/anchors.inc` for the root
   or CA, `src/net/net.c` for the key or algorithm, `src/net/ct_logs.inc` for a
   log. A disclosure about a library we do not use (Brotli, WPS, TLS session
   resumption) gets a one-line "not applicable, because" in the log, with the
   grep that shows it.
2. Reproduce with a key or input anyone can make. The eMudhra keys were
   numbers below 256, so the test mints the same shape in `small_p384_key`
   (`test/differential.py`). A regression row that needs the vendor's secret
   proves nothing.
3. Contain at the narrowest point that works, in this order. Refuse the key by
   its SPKI hash in `tls_blocked_keys`; that survives a trust-store
   regeneration. Drop a root through `ANCHORS_LEFT_OUT` in the anchors
   regenerator, never by hand-editing `anchors.inc`, so the file still
   regenerates byte for byte. Do not rely on revocation or Certificate
   Transparency as the fix: both are off or soft-fail at the default tier, and
   the attacker is on the path.
4. Prove the fix catches the attack. Disable the new check in a scratch copy
   and confirm the new rows fail. A row that passes both ways tests the shape
   of the chain, not the check.
5. Commit to main, then run the net lane on the box from a clean export of
   `HEAD` (the working tree may hold someone else's edits). Fuzz a deep run on
   the parser the bug touched: `MOONWATER_FUZZ_SECONDS` alone does nothing,
   because every target is also capped at 20,000 runs; raise
   `MOONWATER_FUZZ_RUNS` as well. Run the mutation lane on the function you
   added (`--harness net_mutation --functions '^name$' --oracle full`) and kill
   every survivor in your own lines.
6. Write the entry below and list what is still open.

## Log

### MW-2026-001: eMudhra intermediate CA keys computable by anyone

| | |
|---|---|
| Status | Fixed on main, 2026-10-23 |
| Class | Weak key generation in a public CA; forged TLS server certificates |
| Moonwater exposure window | 2026-09-14 16:38 to 2026-10-23 03:25, 38 days 10 hours 47 minutes |
| After public report | 2026-10-17 18:05 to 2026-10-23 03:25, 5 days 9 hours 20 minutes |
| Fixed by | `47cc1cc8` (fix), `08d96ebb` (anchor count check, audit seal) |
| Hardening after the fix | `d958266a` (refuses any EC key anyone can compute, with or without a list) |
| Related | `38ddcbc9` (runs the official Wycheproof vectors in the crypto lane) |

**What happened upstream.** eMudhra generated eight intermediate CA keys in a
key ceremony on 2025-04-30 with too little entropy. The six TLS intermediates
(EM DV, OV and EV TLS CA, each in a G3B and a G3C copy) chain to emSign Root
TLS CA - G3 and then to emSign ECC Root CA - G3. The two OV code-signing
intermediates are the other pair. Each key is a P-384 key whose private scalar
is below 256; the scalars are 156, 230, 115, 56, 155, 4, 66 and 237, and we
recovered all eight ourselves by trying every scalar below 4096 against the
published public-key hashes. Sectigo (AAA Certificate Services) cross-signed
the same public keys, so a hash of the key covers both copies. Anyone could
compute the private keys from the public certificates; Scott Helme reported
doing it in under a second.

| When (UTC) | Event |
|---|---|
| 2025-04-30 | Key ceremony that produced the weak keys |
| 2026-10-17 18:05 | Report reaches the Chrome Root Program |
| 2026-10-17 19:45 | Chrome blocks the six TLS intermediates by key hash (CRLSet) |
| 2026-10-17 22:59 | Firefox block (OneCRL) reaches default installs |
| 2026-10-18, 19 | eMudhra revokes all eight certificates |
| 2026-10-19 | Apple blocks them; Android receives a block via Play Services |
| 2026-10-20 | Microsoft blocks them; Mozilla removes emSign ECC Root CA - G3 (NSS 3.131.1) |
| 2026-10-23 03:25 | Moonwater fix `47cc1cc8` |
| about 2026-10-27 | Chrome's removal of the same root reaches all devices (announced) |

**How Moonwater was exposed.** The trust store has carried the Mozilla server
roots since `d00160d6` (2026-09-14 18:38 +02:00); before that commit it held
three hardcoded anchors, none of them eMudhra. emSign ECC Root CA - G3 was one
of the 120 roots. A server presenting `leaf <- weak intermediate <- root` was
accepted at the default tier, for three reasons:

- The default tier does not require Certificate Transparency. Logged-SCT
  checking exists only at the tight tier (`MOONWATER_STRICT_NET=2`, added in
  `dc56c493`, 2026-10-11 23:19 +02:00). A build at the tight tier refused an
  unlogged certificate and was not exposed.
- Revocation checking (`43ee3519`, 2026-10-09) is soft-fail at the default
  tier: a CRL or OCSP answer that cannot be obtained counts as "no answer" and
  the chain is accepted (`tls_revocation_holds` refuses only at the tight
  tier). The forger's leaf carries no pointers. The genuine intermediate
  certificate does carry one, signed in by the root, and from 2026-10-18 and 19
  the root's CRL listed it; but that CRL is fetched over plain HTTP, and an
  attacker who is on the path drops the fetch. Before 2026-10-18 nothing listed
  the intermediates at all.

An attacker needed a network position to serve the chain. Every client that
goes through `tls_verify_chain` was affected: wget and fetch, NTS key
establishment, and DNS over TLS. The worst case is NTS on a machine whose clock
has never been set: its first answer is believed, so a forged NTS server could
have set the clock to any time after the build date, and every later
certificate decision follows the clock. On a clock that is set, a step past 2
seconds needs a second server's agreement and cannot go back before the last
authenticated time (`clock.good`).

Checked and not exposed. The bowl root-filesystem downloads: every row is a
SHA-256 compiled into `src/bowl.c` or an OpenPGP signature against a pinned
key, and a row that pins nothing is refused, so a forged mirror certificate
serves a file that fails the digest. `moonwater setup update` writes the build
that is running onto a disk and downloads nothing.

**Evidence of exploitation.** None known. Moonwater keeps no log of accepted
chains, so a past acceptance cannot be ruled out from our side. Chrome reports
that no certificate chaining to the six TLS intermediates had been logged to
Certificate Transparency before it blocked them, which bounds only the
CT-visible part.

**The fix (`47cc1cc8`).**

- `tls_blocked_keys` in `src/net/net.c` holds the SHA-256 of each key's DER
  SubjectPublicKeyInfo, the same naming Go, Chrome and Mozilla use.
  `tls_verify_chain` refuses a leaf with one of those keys, never uses a
  served intermediate with one as a step of the path, and refuses a
  caIssuers-fetched certificate with one. The user sees "has been revoked".
  The check runs at every tier.
- emSign ECC Root CA - G3 is out of `src/net/anchors.inc` (119 roots, 39232 key
  bytes), through `ANCHORS_LEFT_OUT` in the regenerator. The bundle the file is
  generated from (Arch ca-certificates-mozilla 3.129) still lists the root, so
  a regeneration from it must keep that entry.

**Tests that fail without the fix.**

- `tls_chains`, five rows. A bench root signs the blocked key 156: a leaf under
  one intermediate, an intermediate in the middle of a chain, and a leaf
  holding the key are refused as revoked (OpenSSL accepts all three). The next
  scalar, 157, is accepted, so the check is not "any small key". A served
  certificate of the blocked key that nothing in the chain needs is accepted.
- With the comparison in `tls_key_blocked` replaced by `if (0)` in a scratch
  copy, the three refusal rows fail ("wget accepts a chain the standards matrix
  says to refuse") and 18 of 21 rows hold.
- `--harness anchors` recomputes the eight hashes from the eight scalars and
  compares them with the table in `net.c`; it also holds `anchors.inc` to 119
  roots. `CHECK_net` checks that neither the Name nor the key of the dropped
  root is an anchor.
- Results on the box from a clean export of `HEAD` after the fix: net lane
  230514 of 230514 on x86-64, 230452 of 230452 on arm64 and riscv64,
  `tls-chains` 1916 of 1916, `anchors` 481 of 481.

**Hardening after the fix (`d958266a`).** The blocklist only helps once someone
names the keys. `tls_small_keys` in `src/net/anchors.inc` holds a key hash for
k*G, k = 1 to 1024, on P-256 and P-384, and `tls_key_blocked` refuses any
certificate whose EC key is on it, at every tier. The eMudhra keys (k below 256)
would have been refused on 2026-10-17 with no list. The same commit audits the
119 built-in roots for the defect (`--harness anchors --weak`): no EC root is
k*G for k below 2^20; no RSA root has a prime factor under 2^24, shares a prime
with another root, falls to 100,000 steps of Fermat's method, or carries the
ROCA fingerprint. The detector fires on planted keys of each kind. What this
does not cover: a weak key that is not a small scalar (a key from a bad but
non-trivial generator), RSA keys on served intermediates (no runtime screen),
and keys outside P-256 and P-384.

Found on the way. The libFuzzer target for path building and anchors
(`tls_verify_fuzz`) did not compile from 2026-10-11 (`dc56c493`, which made the
chain call `tls_trust_holds`) to 2026-10-23: its lift stubbed the old function
name. The fuzz lane's first gate also failed whenever it ran first in a
process, so the failure was not read. Both are fixed (`20734c17`, `d958266a`).
For those 12 days the chain verifier was covered by the unit lanes and
`tls_chains` but not by the fuzzer. Results on the box from a clean export of
`d958266a`: net lane 230515 of 230515 on x86-64 and at the tight tier, 230453
of 230453 on arm64 and riscv64, `tls-chains` 1932 of 1932.

Deep fuzz after the fix, on `20734c17` plus the tests: eight parsers (DER,
OCSP, CRL, TLS handshake, chain verification, HTTP at the default and tight
tiers, DNS, DHCP) for 20 minutes each with the 20,000-run cap lifted, one seed
each, ASan and UBSan: no crash and no sanitizer report. That is 20 minutes a
parser, not an assurance.

**Still open.**

- A machine is fixed only after it boots an image built from `47cc1cc8` or
  later. This entry records the commit on main, not a deployment.
- emSign Root CA - G1, C1 and C3 are still trusted. Chrome is reviewing G1.
  Check the CCADB list and drop them through `ANCHORS_LEFT_OUT` if a root
  program removes one.
- Removing a root or key no longer needs a rebuild: `moonwater security block
  add SHA256|FILE` refuses a key at runtime, `security trust remove` and
  `block add` take an anchor out, and the list is kept in the settings block
  and carried by install and update (`6f93271d`, `19558fe7`, `ef52a335`,
  `d55611a2`). A built-in block (the eMudhra keys, the small-key screen) cannot
  be lifted from the CLI. A next incident of this kind is first handled with
  `block add` on the key hashes, then the source change follows. Not yet
  covered: the mutation lane has not run on the overlay code, and a signed
  feed that would deliver blocks without a person typing them is designed, not
  built.
- Nothing watches for CA incidents. This one was found on 2026-10-23 during a
  review of recent disclosures, 5 days 7 hours 40 minutes after the first
  browser block.

**Sources.** Press and vendor posts, read on 2026-10-23; the primary records
(Mozilla bug 2083449, the CCADB posts) were not read.
[Chrome](https://blog.google/security/protecting-chrome-and-android-users-from-compromised-emudhra-intermediate-ca-certificates/),
[The Hacker News](https://thehackernews.com/2026/10/eight-emudhra-ca-keys-compromised.html),
[Dennis Jackson's list of affected certificates](https://emudhra.we-have-a-problem.com/).
The eight key hashes were taken from Go's fix
(`golang/go@7f86f957633e`) and from Jackson's `impacted-intermediates.json`;
they agree, and the scalars above reproduce them.

### MW-2026-030: locale status lines printed a disk's /root words raw

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26 (awaiting cherry-pick to main) |
| Class | Terminal escape / line injection from attacker-controlled on-disk state printed by a root process |
| Attacker | 6 (controls the data partition or the booted stick while the machine is off) and, today, a root-launched bowl program (case R), which can write `/root`. Not a genuinely unprivileged kernel uid (case U): `/root` is 0700 root-owned, so case U cannot plant these files. |
| Fixed by | (worktree) `src/sh/host.c` locale sinks, `test/differential.py` rows |

**What happened.** `moonwater keyboard`, `moonwater timezone`, `moonwater ntp`,
`moonwater time sync` and `moonwater status` print words they read from `/root`
with a bare `%s`: `/root/keyboard` (`locale_keyboard_say`), `/root/timezone`
and `/root/timezone.mode` (`locale_zone_line`, `locale_status_lines`,
`locale_zone_how`'s country of an auto mode, and the `timezone auto` error that
names the kept zone when the network cannot be asked), `/root/ntp.server` and
`/root/ntp.nts.servers` (`locale_ntp_status`, `locale_nts_status`), and the
server that answered a time query (`locale_ntp_answered`, named in the
`time:`/`time moved` lines of `moonwater time sync`). A `%s`
argument is sent through `plain_out`/`plain_err` (src/sh/file.c), which escapes
ESC and the C1 controls but by design keeps a newline, a tab and valid high
UTF-8 ("a line or a tab is the text's own"). `host_read_word` strips only the
trailing newline of a word, so an embedded one survives. A disk whose
`/root/timezone` reads `Europe/Berlin\n[Moonwater] ...` makes `moonwater
status` print a second line that looks like the machine's own; a word with a
bidi override (U+202E) reorders the terminal line. The genuine writers
(`locale_keyboard_set`, `locale_zone_set`, `locale_nts_servers`) validate
against a table or a name grammar, so a word only becomes malformed when it is
planted on disk, never through the CLI.

**How it was reproduced.** In a booted guest (serial root shell), planting the
word and reading the output raw with `od -c`:

```
printf 'us\n[Moonwater] INJECTED keyboard line' > /root/keyboard
moonwater keyboard | od -c
0000000   [   M   o   o   n   w   a   t   e   r   ]       k   e   y   b
0000020   o   a   r   d       u   s  \n   [   M   o   o   n   w   a   t
0000040   e   r   ]  \n
```

and the same with `/root/timezone` (~70 bytes of forged line) and
`/root/ntp.server`. The word reaches the sink identically however it is
planted, so the sandbox rows below plant it as the run's own root.

**The fix.** Each sink spells the `/root` word through `writer_terminal_name`
(`%w`) instead of `%s`. `writer_terminal_name` escapes control bytes, tab and
high bytes to `\xNN` inside the formatter, before any writer, so the newline,
the tab and the bidi override can no longer forge a line or reorder one. A
legitimate keyboard code, zone name, country code or NTP hostname is ASCII and
is shown unchanged, so no good input moves. Fixed at the sink, not the source,
because the words are also read by other programs (the compositor watches
`/root/keyboard`): the reader is left alone and each printer is made safe.

**Tests that fail without the fix** (`--harness moonwater_cli`, lane `cli`):

- `moonwater keyboard spells a newline in a /root word, not a forged line`
- `moonwater timezone spells a newline in a /root word, not a forged line`
- `moonwater ntp spells a newline in a /root word, not a forged line`
- `moonwater timezone spells a bidi override in /root/timezone, not raw`
- `moonwater timezone spells a newline in /root/timezone.mode, not a forged line`
- `moonwater timezone auto spells the kept zone in its error, not a forged line`

Each plants a hostile word under `/root` in the cli sandbox (no network), runs
the verb, and reads the output raw. On the pre-fix `host.c` (the sinks reverted
to `%s`) the newline rows see a bare `[Moonwater]`-shaped line and the bidi row
sees the raw U+202E, and are RED; on the fixed tree the bytes are spelled
`\xNN` on the verb's own line and the rows are GREEN. The `time:`/`time moved`
server lines (`locale_ntp_answered`) are the same change but need a real NTP
answer to fire, which the offline sandbox cannot give, so they are fixed as a
one-line-per-sink hardening with no row of their own; the server name they
print is the same `/root`-chosen word the `moonwater ntp` row covers.

**Still open.** The fix covers the locale cluster. Other verbs that print a
disk's or a device's word were audited and route through an escaper already:
SSIDs through `radio_display`, USB product/serial through `usb_shown`/`usb_key`,
link peer names through `link_name_good`, the machine name through `name_valid`,
an image's build through `host_plain_line`, DNS servers as numeric addresses.
A general-purpose high-byte escape of non-ASCII names (bidi in an SSID that may
legitimately be non-ASCII) is a separate decision and is not changed here.
### MW-2026-040: a Waterlink group grant taken back left its members holding it

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26; awaiting cherry-pick to main |
| Class | Stale authorization: revoking a grant from a group did not reach the machines the group had already paired |
| Attacker | A linked peer (attacker 8) that was paired through a group, after the operator narrowed the group's grants to take an authority back |
| Reachable | `moonwater link group NAME SECRET allow GRANT...`, src/waterlink/command.c `link_group_join` |

**What happened.** A machine joins a group with a set of grants
(`link group lab SECRET allow run shell`). Every peer that later pairs itself
through that group is written into `/root/link.peers` with the group's grants
copied into its own `may` word (`link_pair_keep`, src/waterlink/nearby.c), and
carries the group's mark. When the operator then narrows the group
(`link group lab SECRET allow shell`, dropping `run`), the old code wrote the
new grants onto the group record alone. The peers keep the `run` bit in their
own records, and `link_request`/`link_sessions_granted` read authority from the
peer record, not the group. So a machine the operator believed they had taken
`run` from went on running commands as root. Revoking a grant from a group was
silently a no-op for everyone already in it; the only way to take it back was
`link remove` or `link deny` on each peer by name, which the operator has no
reason to know they must do.

This is the group analogue of the revocation the lane already proves for a peer
named by hand (`link deny NAME run`). A group is the form made for a fleet, so
it is the form whose revocation matters most.

**Reproduced.** In the two-namespace link lane (`sh test/run link`), machine a
pairs itself to b through group `lab` (granted `run shell`). b runs
`link group lab SECRET allow shell`, then a runs
`moonwater link run <a> 'echo kept-run'`: on the old code the command runs and
prints `kept-run` (status 0). The peer line on b still reads
`may shell run files, in group lab`.

**The fix.** `link_group_join`, when `allow` narrows a group's grants, computes
`taken = old_may & ~new_may`, and, under the peers lock it already holds, clears
those bits from every peer carrying the group's mark before it saves the group
record. Members lose the authority first (peers written, then the group), so a
failure between the two files leaves less granted than asked, never more; a
peers file that cannot be read or written changes neither. A grant the group
*starts* giving is not pushed onto existing members (they are each given more by
name as before). Machines paired by hand carry mark 0 and are never touched. The
verb now says how many members lost what.

**Tests that fail without the fix.** `waterlink-link` (`test/differential.py
--harness waterlink_link`, run by `lane_link` / `sh test/run link`), the group
block, new rows:
- `sec: a machine lab paired may no longer run once b takes run from lab` —
  RED on the parent tree (the far command runs, status 0, `kept-run`), GREEN
  after (`run is not granted`, status 255, no output).
- `sec: and keeps shell, which lab still grants, and files, which lab never
  did` — holds that narrowing takes only the dropped bit, not every bit.
- `sec: a machine paired by hand keeps the run lab took from its members` —
  holds that mark-0 records are untouched.
- `saying lab's grants again as they are takes nothing from its member` —
  a no-op `allow` of the current grants changes nothing.

Box run (`/home/dawn/sec-4`): parent tree `waterlink-link` 198 of 202 (the two
new `sec:` grant rows and the `port` row below red); with the fix 201 of 202.
The one remaining red (`a FIFO at the groups and the machines is read as
nothing`) is a pre-existing lane failure unrelated to this change (a FIFO
planted at `/root/link.groups` by the harness as root; present on the parent
tree before any of my edits) and is reported separately as open.

**Still open.** `link_pair_keep` copies grants at pairing time by design (a
member's record is its own authority), so this fix keeps the two in step only at
the moment the group is narrowed; a grant the group gives is still a
pair-time snapshot. The mutation lane has not been run on the new branch in
`link_group_join`.

### MW-2026-041: a machine could be named `port`, colliding with the `port` verb

| | |
|---|---|
| Status | Fixed on the worktree branch, 2026-10-26; awaiting cherry-pick to main |
| Class | Verb/name namespace collision; a peer name that shadows a command word |
| Attacker | Operator error weaponized by confusion: `link port ...` meant for a peer named `port` instead moves this machine's own listener |
| Reachable | src/waterlink/service.c `link_words`, used by `link_name_good` |

**What happened.** `moonwater link port [N|auto]` became a verb in `b5ec7de8`
(2026-10-06), but `port` was not added to `link_words`, the list of words
`link_name_good` refuses as machine names. So `link add port KEY HOST`
succeeded, and a machine called `port` could be linked. Afterwards
`moonwater link port` (meant as "a terminal on the machine called port") is read
as the port verb and prints this machine's listener port; `moonwater link port
22360` (meant as "run `22360` on port"? — or simply a fat-fingered peer command)
is read as `link port 22360` and silently rebinds this machine's own Waterlink
listener to UDP 22360 at the next `link on`. The other verb words
(`on`, `off`, `pair`, `add`, `log`, …) are all refused as names; `port` was the
one that slipped through when the verb was added, so the one reserved word a
machine could still be called.

No peer could exploit this against another machine — a peer name is chosen
locally — but it is a real confused-deputy footgun: a name the operator picks
for a peer turns a later `link port` from a command on that peer into a change
to the local listener. The invariant the other verb words already keep (a
machine is never called by a word link uses) was violated for exactly one word.

**Reproduced.** Link lane, new row in the existing "a machine cannot be called
WORD" loop: `moonwater link add port KEY` succeeds on the parent tree (the peer
is stored) and the row `a machine cannot be called port` is RED; with `port`
added to `link_words` the add is refused (`is not a name`) and the row is GREEN.

**The fix.** `port` added to `link_words` in src/waterlink/service.c, one line,
so `link_name_good` refuses it as it refuses every other verb word. A peer
already saved under the name `port` from before the fix still reads (the stored
record is not re-validated against the word list); `link remove port` drops it.

**Tests that fail without the fix.** `waterlink-link`, the "a machine cannot be
called WORD" loop, `port` added to the word tuple: RED on the parent tree, GREEN
after. Box run as above.

**Still open.** Nothing further; the three-letter and shorter codes are a
separate space (`link_code_shaped`) and were never names.
### MW-2026-050: the compositor's window table was handed to any program

| | |
|---|---|
| Status | Fixed on the sec-5 worktree, 2026-10-26 (coordinator cherry-picks to main) |
| Class | Missing authorization on a shared root resource; confused-deputy keystroke injection into a root shell, and disclosure of a root window's pixels |
| Attacker | (8) a compromised bowl program, (1) a local unprivileged process |
| Fixed by | this commit (`src/canvas/window.c`, `src/canvas/canvas.c`, `src/canvas/compositor.c`) |

**What it was.** The compositor (a root process) shares its window state as one
mapping, `/run/moonwater/windows`: a header, then every window's two pixel
buffers and every window's input ring (`event_head`, `event_type`, `event_a`,
`event_b`, `focus`, `lift`, `pid`), all writable by whoever maps it. The design
has every window program map the whole table and read and write the fields of
its row; it presumes mutual trust. Two things let a program that is not a
trusted desktop program in: the file and its door FIFOs were made `0666`
(world read-write, `compositor_table`/`compositor_table_memfd`), and the kernel
handed a descriptor of the table to any caller of `/dev/spark`'s
`WINDOWS_IOCTL_TABLE_GET` with no capability check (`windows_table_get`,
`src/canvas/canvas.c`) -- the comment even said "any program takes a descriptor
of it". The `WINDOWS_IOCTL_TABLE_SET` giver side already required
`CAP_SYS_ADMIN`; the get side did not.

A program that maps the table can: read the pixel buffer of any window,
including the Canvas terminal that runs a **root** shell (screen-scrape a
password as it is typed, a key, anything on screen); write into the focused
window's event ring to inject keystrokes into that root shell (a confused
deputy: arbitrary commands as root); set `focus`, bump `lift`, and overwrite
`pid`. The terminal the machine script starts (`desktop_terminal`) is a root
shell, so injection there is root command execution.

**Reachability, case R and case U.** Today a bowl program started from root (the
Canvas terminal, the serial shell, a `/bowls/bin` launcher -- a KDE session
`moonwater desktop` starts is one) runs as real kuid 0 with full capabilities
and sees the host `/run/moonwater` (agent 4's finding; agent 1 is making bowl
programs a real unprivileged uid). So **case R** (today): a root-launched bowl
already has `CAP_SYS_ADMIN` and is inside the trust boundary -- root is not a
wall, and this fix does not claim to stop it; that is the bowl boundary agent 1
closes. **Case U** (a genuinely unprivileged kernel uid: a local user outside
bowls now, and every bowl program after agent 1's fix): before this fix such a
process took the table through the ioctl and read and wrote it; after it, the
ioctl returns `EPERM` and the `0600` files return `EACCES`. The fix is what
makes case U safe regardless of where the bowl boundary ends up.

Which window-table requests stay available to a non-root program: **none.**
Every legitimate window-table program is root (the compositor, and the
terminal, log, launcher and settings the root machine script starts); a
KDE/Chrome-style client speaks Wayland/X, not this table. Isolated unprivileged
windows are not a feature today, and the single shared writable mapping could
not enforce per-row isolation if they were (that needs a per-client protocol).
So the boundary is root/not-root, and nothing a real client does breaks.

**How it was reproduced.** A static helper `tableget` on a stick, in a booted
guest with the compositor up: it opens `/dev/spark`, calls
`WINDOWS_IOCTL_TABLE_GET`, maps the result and reads the live output size out of
the header, then does the same dropped to uid 65534. On `final-head.efi` (main
before the fix) the unprivileged run mapped the table (`GET ok 1280x800`) and
the files were `666`; on an image built from the fix the unprivileged run is
refused (`GET denied 1`) while root still succeeds, and the files are `600`.
Lane `canvas`, scenario `g1` (`CANVAS_ONLY=g1 sh test/run canvas`).

**The fix.**
- `src/canvas/window.c`: `WINDOWS_MODE` is `0600`, the one mode the compositor
  gives the table, its memfd and the door FIFOs.
- `src/canvas/canvas.c` `windows_table_get`: `if (!capable(CAP_SYS_ADMIN))
  return -EPERM;` before it hands out a descriptor, matching
  `windows_table_set`. `capable(CAP_SYS_ADMIN)` is against the initial user
  namespace, so a process that is only root in its own namespace (an
  `unshare -r` bowl) is refused.
- `src/canvas/compositor.c`: the table file, its memfd and the door FIFOs are
  created and `fchmod`ed `WINDOWS_MODE` instead of `0666`.

**Tests that fail without the fix.**
- `canvas` lane, scenario `g1`, four rows: g1.1 root still takes the table
  (`ok`), g1.2 an unprivileged process is refused it (`denied`), g1.3 the table
  is `600`, g1.4 the doors are `600`. Run against `final-head.efi` (pre-fix)
  g1.2 reads `ok`, g1.3 and g1.4 read `666`.
- `CHECK_socket` `windows_header`: two rows pin `WINDOWS_MODE == 0600` and
  `(WINDOWS_MODE & 0077) == 0`; a flip back to `0666` fails them.

**Still open.**
- The single shared writable table remains the design. A program that is root
  (case R bowls today, and root itself -- root is not a wall) is inside it. An
  isolated unprivileged window would need a per-client protocol, not this
  mapping; that is an owner decision, not built here.
- Behaviour change, stated: `moonwater debug` run by a non-root user can no
  longer read the Canvas readout (it used the same open path) and prints "no
  compositor is running". The verb is a diagnostic; its code is in the
  contended `src/sh/host.c` and was left unchanged to merge cleanly.
- The memfd route (`/run/moonwater/windows` as a `/proc/<pid>/fd` symlink) was
  already held for non-root by `/proc` tracing when the table is a memfd; the
  `0600` mode is what holds the regular-file fallback (a headless or no-card
  compositor) and a local non-root user there.
- The door FIFOs being `0600` also stops a non-root local process knocking the
  compositor's door loop (the per-turn read bound added earlier capped the
  work; this removes the reach).
### MW-2026-020: a compositor window door any program may hold open against its row

| | |
|---|---|
| Status | Fixed on this branch, 2026-10-26 (awaiting cherry-pick) |
| Class | World-writable control FIFO a root process acts on; denial of service |
| Attacker | (1) a local unprivileged process; (8) a bowl program once it is a genuine unprivileged uid. A root-launched bowl today is host uid 0 (case R, agent 4's finding) and reaches it as root; that path is moot once bowls are an unprivileged uid, but the hole remains for case U |
| Fixed by | this commit (`src/canvas/window.c`, `src/canvas/compositor.c`) |

**What it was.** The compositor (`/compositor`, root) gives each window a row of
a shared table and a FIFO door at `/run/moonwater/window.N` (and a wake beside
it, `window.N.wake`). `compositor_table` made both with mode `0666`
(`mknodat 0010666` then `fchmodat 0666`). The compositor frees a row --
`compositor_leave`, which takes the window off the screen, gives its pixels
back and clears its pid -- when the door's poll returns `POLLHUP`, which a read
end sees only when the door's *last writer* has closed, i.e. when the program
that owns the window has ended.

A door the whole machine may write is a door the whole machine may hold open. A
local process that is not the window's owner opens `window.N` for writing and
keeps it open; the writer count never falls to zero when the real owner ends,
so the compositor never hears it leave and never frees the row. The window
stays as a ghost, and with all thirty-two doors held and the windows behind
them gone, no row is ever free again: the desktop can open no new window. (A
flood of knock bytes on a held door also keeps the root compositor's poll loop
hot, bounded at 64 reads a turn but woken every turn.)

**Why it is denial of service and not worse.** The window *table* -- every
field a program and the compositor read and write, and the pixels -- is the
compositor's `memfd`, published as a symlink to `/proc/PID/fd` of the root
compositor. A process that is not root (or the compositor's own user) cannot
open that descriptor (`ptrace_may_access`), so it cannot map the table and
cannot be a window at all; the door carries only a content-free knock that
tells the compositor to re-read the row's fields, which only root can have
written. So the exposure is the row never freed (a dead window's pixels held,
new windows refused), not reading or writing another window's state.

**Reproduced.** Booted guest (`final-head.efi`, current main, the Canvas
desktop up):

```
# stat -c %a /run/moonwater/window.0            -> 666
# setpriv --reuid 1000 --regid 1000 --clear-groups \
      sh -c 'exec 9>/run/moonwater/window.0'    -> succeeds (a non-root
                                                   program holds the door)
# as uid 65534: head -c 16 /run/moonwater/windows -> denied (cannot map the
                                                     table: knock-only)
```

On an image built from the fix the door is `600` and the non-root open is
denied (`EACCES`).

**The fix.** `WINDOWS_DOOR_MODE` is `0600` (`src/canvas/window.c`), and
`compositor_table` makes the door and its wake with it (`src/canvas/compositor.c`).
Every window is a program a desktop session started, which is root, and the
compositor is root, so a root-only door breaks no client that can function (a
non-root program cannot map the table and so cannot be a window). A future
design that wants an unprivileged program to be a window must hand it the door
and the table as descriptors, not leave a world-writable FIFO on the machine's
shared `/run`.

**Tests that fail without the fix.**

- `desktop` lane, the M5 terminal check, two rows: `M5 a window door is
  root-owned 0600` (got `666` on the pre-fix image, `600` on the fix) and `M5 a
  non-root program cannot open a window door` (got `opened`, then `denied`).
- `checks.c` `windows_client`, row `a window door is root-only`, pins
  `WINDOWS_DOOR_MODE == 0600 && (WINDOWS_DOOR_MODE & 0077) == 0` (kit/term
  lane); it is red if the constant is put back to `0666`.
- Red-on-old shown by hand: the exact commands the M5 rows parse, run against
  `final-head.efi`, print `MODE=666` and `NONROOT=opened`; against the fix's
  image, `MODE=600` and `NONROOT=denied`.

**Still open.**

- The fallback table at `WINDOWS_TABLE` for a machine with no card
  (`compositor_table`'s no-`memfd` branch) is still made `0666`; a headless
  machine has no desktop, but it could be `0600` too, left for a follow-up.
- Reclamation is still `POLLHUP`-only. Binding a row's freeing to the owner
  pid's liveness (`kill(pid, 0)`) would reclaim a dead owner's row even if a
  squatter holds the door, and is the deeper fix should an unprivileged
  window-client design ever reopen the door to non-root. Not built.
- The mutation lane has not been run on the changed lines.

## Reviews with no exposure found

Disclosures read against the code on 2026-10-23, each with the check that was
made. Advisory numbers are as secondary sources gave them; none was checked
against NVD. A row here is evidence the class was looked at on that date, not a
promise about later code.

| Class (public disclosure) | Result | Check made |
|---|---|---|
| Host treated as IP by one component and as a name by another (Node.js, Oct 2026) | Not exposed | `string_to_host` takes strict dotted quads only; the connect path and `tls_general_name_match` classify a host by that one function; `--harness tls_hostnames` holds the name check to OpenSSL over 1531 pairs, NUL bytes and partial wildcards included |
| X.509 name-constraint bypass via wildcards, case, mixed name types, unconstrained middle CA (wolfSSL, GnuTLS, pyca, Go, AWS-LC, 2026) | Not exposed | `tls_chains --random`: 1,500 random constrained chains over three seeds, none accepted that OpenSSL refuses; wildcard under an excluded subtree refused, including in another letter case |
| ECDSA edge cases: r or s of 0, point at infinity, invalid curve points, DER laxness (Psychic Signatures 2022, OSEC-2026-13) | Not exposed | Official Wycheproof vectors through the production crypto on x86-64, arm64 and riscv64: 8837 of 8837 each (`MOONWATER_WYCHEPROOF`, lane `net_crypto`) |
| CurveBall (explicit EC parameters), MD5 or SHA-1 signatures, inner and outer signature algorithm mismatch | Not exposed | `tls_parse_cert` accepts the P-256 and P-384 OIDs only, ECDSA-SHA2 and RSA-PKCS1-SHA2 only, and requires the two algorithm fields to match |
| DHCP option injection into scripts or lease files (dracut, FreeBSD dhclient, TP-Link, 2026); TunnelVision, option 121 routes (2024) | Not exposed | The client asks for options 1, 3 and 6 and reads 1, 3, 6, 51, 54, 58 and 59 as binary values; no string option is consumed and no route option is read |
| Credentials carried across a redirect (curl 2026 series; wget CVE-2021-31879) | Not exposed | The HTTP client has no userinfo, `Authorization`, cookies, netrc or proxy; userinfo in a URL or `Location` is refused |
| Brotli decoder write (Node.js, Oct 2026); response decompression bombs | Not exposed | No Brotli in the tree; the client sends no `Accept-Encoding` and decodes no content encoding |
| TLS session-ticket forgery (nginx, Oct 2026) | Not exposed | The client offers no PSK and resumes nothing |
| WPS setup bypass (Broadcom, Oct 2026) | Not exposed | No WPS code |
| Unbound source-port side channel; DNS cache poisoning in general | Not exposed | The stub draws a CSPRNG source port per query on its own connected socket, randomizes the question's letter case, checks the ID and the echoed question |
| PwnEscape, CVE-2026-99999 (kernel IPv4 checksum offset, Oct 2026) | Not reachable as configured | The profiles have no TUN or vhost; AF_PACKET with a virtio header needs CAP_NET_RAW. Kernel pinned at 7.3; confirm `ip_check_csum_start` is in the tarball at the next fetch |
| NTP time shifting (published attacks on ntpd, 2021 and after) | Bounded | A set clock steps at most 2 s on one server's word; a bigger step needs a second server; no step goes back before the last authenticated time. No cumulative cap across polls (about 2 s per poll, polls at 4 to 30 minutes) |
| Wi-Fi downgrade and Dragonblood (WPA3 to WPA2, SAE timing) | Read, not exercised today | SAE with hash-to-element, hunting fixed at 40 whole passes, PMF required where offered, and a network joined with WPA3 is never joined with WPA2 (`src/sh/host.c`) |
| Output file named by the server (wget CVE-2016-4971 and the Content-Disposition family); a symlink planted where the file will go | Not exposed | A hostile local server against the built shell: `Content-Disposition` names with `../`, with an absolute path and with `filename*`, and redirects to `..%2f`, `/../` and a query holding `../`. Every file landed inside the working directory, named from the requested URL, not from the header or the redirect. A `Location` holding a newline or ESC byte is refused. An existing `index.html` symlink was not followed (wget wrote `index.html.1`, the target was unchanged) |
| Hostile server: redirect loops, huge or many headers, lengths past 2^64, slow drip | Bounded | 20 redirections, then exit 8. The head is capped at 16 KiB (`HTTP_HEAD_MAX`): a 64 MiB header line and 2 million headers were refused in under a second at 13 to 14 MB resident. `Content-Length: 99999999999999999999` and a chunk size of 2^64-1 are refused. A one-byte-per-half-second drip is held for as long as the read timeout resets, as GNU wget does |
| Unsigned bootstrap downloads | Not exposed | Every bowl row is a compiled-in SHA-256 or a pinned OpenPGP key; a row that pins nothing is refused (`src/bowl.c`) |
| SSH (Terrapin, regreSSHion) | Not applicable | No SSH implementation in the tree; OpenSSH inside a bowl is the distribution's package |
