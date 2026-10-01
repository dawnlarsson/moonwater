# Network dependency closure

This is the evidence boundary for `src/net/net.c`. “The network tests pass” is
not enough if the byte, allocation, socket or cryptographic primitive below the
parser is untested. Conversely, testing every unrelated routine in the whole
of `lib.c` says little about the subset the network stack actually reaches.

`python3 test/differential.py --harness net_dependency_closure` extracts every
direct call from `net.c` into the shared `memory_*`, `string_*`, `byte_reader_*`,
`byte_store_*`, `array_store_*`, `network_*`, `socket_*`, `system_*` and numeric
families. It compares that set to a reviewed manifest. Adding or removing a
primitive makes the harness fail until this table and its evidence assignment
are reviewed. The harness also proves that each assigned lane or harness is
registered and invoked by `test/run`.

## Reachable foundation and evidence

| Foundation used by `net.c` | Adversarial evidence |
| --- | --- |
| `memory_*` and `string_*` spans, overlap copies, comparisons and bounded conversions | the `standard`, `strings`, `exact`, `scan`, `leaving`, `stack` and sanitizer lanes; every network parser is also driven through these operations by `CHECK_net` and its fuzz target |
| `byte_reader_*` and `byte_store_*` cursors | exact/one-over and sticky-failure checks in `CHECK_net`; parser-specific ASan/UBSan fuzz targets; MSan hosted lifts; `security_hygiene` forbids listed hostile-byte parsers from bypassing `byte_reader` |
| `array_store_*` allocation/growth | `allocator` and `reserve`, plus first- and mid-growth fault injection in `CHECK_net` and the resource-exhaustion rows of `SECURITY_TEST_MATRIX.md` |
| byte-order loads/stores and `positive_into` | `socket`, `numbers`, all-architecture `CHECK_net`, and protocol differential models |
| `socket_*`, `system_*` and `network_stream_*` I/O | `socket`, `net`, `netem`, `wget_mutation`, `tls_peer`, descriptor exhaustion, short I/O, `EINTR`, `EAGAIN`, deadline and mid-path failure injection |
| SHA-1/256/384, HMAC, HKDF, PBKDF2, AES-128-GCM, X25519, P-256/P-384 ECDH/ECDSA and RSA PKCS#1/PSS | generated OpenSSL differential vectors through production `crypto_*` and `lib.c` assembly on each available architecture; `crypto_fuzz` against libcrypto; optional Wycheproof vectors; branchless-body and X25519 instruction-count checks |
| TLS/X.509 policy layered on those primitives | record/handshake/DER/path fuzz targets, scripted TLS peers with real crypto, OpenSSL plus Go path-validation oracles, hostname/date matrices and public-chain corpus |

## What the closure gate proves

The gate proves **inventory closure**, not absence of defects:

1. every shared primitive family directly called by `net.c` is in the reviewed
   manifest;
2. every manifest group names evidence which is wired into `test/run`;
3. production crypto is checked separately at both layers: `net.c`'s C policy
   and composition against OpenSSL under fuzzing, and `lib.c`'s architecture
   bodies against generated vectors on each architecture available; and
4. a new dependency cannot enter silently.

It does not make skipped architecture, sanitizer, namespace, external corpus
or oracle runs into passes. A release can claim a closed evidence loop only
when the report records successful `net`, `socket`, `crypto_vectors`,
`crypto_fuzz`, `msan`, `netem`, and architecture runs, with the external
Wycheproof/public-chain campaigns attached. The currently parked security CI
and optional corpora therefore remain release-assurance gaps, not exceptions
that this manifest hides.

For a release candidate, architecture skips are made fatal rather than merely
reported:

```sh
MOONWATER_REQUIRE_ARCHES=1 MW_UBSAN=1 sh test/run net
```

This requires native-or-qemu x86-64, ARM64 and RISC-V toolchains and runs the
freestanding stack with UBSan trapping. Hosted ASan/UBSan and MSan remain the
`fuzz` and `msan` lanes; a release report must contain those results as well.
