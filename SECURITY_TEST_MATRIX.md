# Security test matrix

| Threat family | Evidence | Command |
| --- | --- | --- |
| Network byte order, socket boundaries | `socket` lane on each available architecture | `sh test/run socket` |
| Netlink source, sequence, attributes; DNS; HTTP; TLS; DHCP | freestanding network checks | `sh test/run net` |
| UDP replay and DHCP reacquisition authorization | identity mutation, queued prior transaction, exhaustive state cross-product | `sh test/run net machine` |
| Certificate path semantics | generated chains against OpenSSL | `python3 test/differential.py --harness tls_chains` |
| SNTP nonce, ancillary timestamps, timing arithmetic, server selection | machine checks | `sh test/run machine` |
| Shell parsing, expansion, environment, status and effects | generated Bash/Dash comparison | `sh test/run shell builtins` |
| File traversal, symlink and replacement behavior | effect-based coreutils differential | `sh test/run files` |
| Tar paths and archive framing | tar lane and compression grammar | `sh test/run tar compression` |
| Codec hostile lengths and guard pages | codec-specific and floor lanes | `sh test/run codec compression_floor` |
| Waterlink replay, seal and lossy delivery (separate review scope) | pure transform and namespace integration | `sh test/run waterlink link` |
| Whole available suite | all locally supported lanes | `sh test/run` |

## Required test shapes for a new parser

Every new externally reachable parser must add:

1. empty, shortest, exact-limit, and one-over-limit objects;
2. truncation at every byte boundary;
3. every discriminator/tag value, including unknown values;
4. integer encodings at every width and overflow;
5. duplicate, reordered, conflicting, and trailing fields;
6. every stream split point and partial read/write behavior;
7. a fixed resource ceiling and a test which reaches it;
8. identity mutations for every field used to authorize a reply;
9. fault injection for allocation, randomness, time, and I/O where applicable;
10. a differential oracle or a documented reason no independent oracle exists.

Passing rows are evidence for a particular build and environment, not a
permanent certification. Unsupported architecture, namespace, sanitizer, or
oracle lanes must be reported as not run rather than silently counted as pass.

## Oracle quality

A differential oracle is not enough by itself: two implementations can agree
on the same mistake. The TLS chain grammar therefore carries an independent
accept/refuse matrix for every mutation and key type as well as comparing the
two implementations. Invalid SAN, validity, CA, path-length, key-usage, EKU,
critical-extension, issuer, and trust-anchor cases must be rejected; valid
depths, absent optional leaf/issuer constraints, non-critical basic constraints,
a served root, and unknown non-critical extensions must be accepted. Extension
policy is exercised at both leaves and intermediates. Deliberately stricter
Moonwater policy is named at the individual case.
