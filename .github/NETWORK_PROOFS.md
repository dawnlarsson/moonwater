# Machine-checked network lemmas

Tests sample executions. Fuzzers search executions. The `net_math_proof`
harness is narrower: it discharges finite mathematical claims over the complete
input domain of selected primitives used by `src/net/net.c`.

## Current proof obligations

1. **DHCP mask characterization (32-bit bit-vectors).** The production test
   `!(~mask & (~mask + 1))` accepts exactly a run of leading one bits followed
   by zero bits (with zero retained as the documented omitted-mask value). The
   checker bit-blasts addition and `&` one bit at a time. Dynamic programming
   merges equivalent carry/property states, so all 2^32 masks are covered
   without sampling or enumerating four billion values.
2. **AES GF(2^8) multiplication.** The production
   `crypto_aes_field_multiply` is compiled out of `net.c` and compared for all
   65,536 ordered byte pairs with a separate carryless polynomial product and
   long-division reduction modulo `x^8 + x^4 + x^3 + x + 1` (not the
   production shift-and-xtime recurrence).
3. **AES S-box.** The production fixed-addition-chain S-box is evaluated for
   all 256 bytes against a separate exponentiation-to-254 inverse and affine
   transform. The checker also proves the resulting mapping is a permutation.
4. **HKDF block counter.** For every permitted output length, including zero
   and the 8,160-byte ceiling, the arithmetic proof checks that exactly
   `ceil(L/32)` blocks are emitted, each copy is in bounds, and counters are
   precisely 1 through 255. The admitted and refused integer intervals are
   proved to partition the entire `positive` domain, and the production guard
   is pinned to that boundary.
5. **Netlink width/alignment arithmetic.** Boundary proofs show that every
   admitted attribute length fits `rta_len`, every admitted message length fits
   `nlmsg_len`, four-byte alignment cannot wrap inside those admitted domains,
   and the first refused neighbor is outside the wire field.
6. **Absolute-deadline clock faults.** The production deadline arithmetic is
   compiled over a hostile monotonic clock. Zero readings, backward jumps,
   exact starts, forward jumps past expiry, subsecond remainder arithmetic,
   invalid nanoseconds and duration overflow are injected; every discontinuity
   fails closed and an overflowing duration saturates rather than wraps.

The harness compiles the production AES functions with ASan and UBSan. The
other obligations are exact bit-vector/integer proof checkers whose production
expressions and constants are pinned in the harness; source drift makes the
proof fail rather than silently proving a different formula.

## Claim boundary

These are mathematical proofs of the stated lemmas, not of the whole network
stack. They do not prove liveness, kernel behavior, protocol authentication,
X.509 policy, side-channel freedom of arbitrary compiled code, or memory safety
outside the compiled proof subject. The dependency-closure gate identifies the
next shared primitive requiring an obligation; fuzzing, sanitizers,
differential peers and namespace scheduling remain independent necessary
evidence.
