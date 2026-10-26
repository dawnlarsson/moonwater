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
