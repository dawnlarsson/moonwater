# TLS fuzz seed corpora

Hostile seeds for the ASan/UBSan libFuzzer harnesses in
`python3 test/differential.py --harness tls_der_fuzz` /
`tls_hs_fuzz`. Expected `TLS_FAIL` is ignored; only sanitizer aborts fail.

**Source of truth is hex in `generate_seeds.py`.** The `*.bin` files under
`tls_der/` and `tls_hs/` are gitignored working copies. Harnesses and
`sh test/fuzz_net` call the generator before use:

```sh
python3 test/fuzz_corpus/generate_seeds.py
```

| Directory | Harness | Parsers |
| --- | --- | --- |
| `tls_der/` | `tls_der_fuzz` | `tls_parse_extensions`, `tls_parse_cert`, Certificate-list framing (`tls_certificate_body_open` walk) |
| `tls_hs/` | `tls_hs_fuzz` | `tls_handshake_one_append`, `tls_encrypted_flight_append` (`tls=null`) |

Seeds are hostile/truncated/overlong/leftover fixtures derived from the
deterministic refuse cases in `test/checks.c`. Empty stubs are not useful;
keep non-empty bytes except the intentional `empty.bin` controls. To add a
seed, append hex to `SEEDS` in `generate_seeds.py` — do not commit `.bin`
files.

## Budgets (do not change lane_net smoke)

| Mode | Command | Default budget |
| --- | --- | --- |
| lane_net smoke | `sh test/run net` | 20 000 runs / 5 s (leave `MOONWATER_FUZZ_*` unset) |
| Continuous local | `sh test/fuzz_net` | unbounded runs / 3600 s |
| Release attach | `sh test/fuzz_net --report` | same as smoke unless `MOONWATER_FUZZ_*` set |
| Deeper local campaign | `sh test/fuzz_campaign` | 200 000 runs / 120 s + report |

Deeper campaign:

```sh
sh test/fuzz_campaign
```

Reports land under `artifacts/` (gitignored). For a release, attach the report
plus `generate_seeds.py` (or a tarball of materialized `tls_der/` + `tls_hs/`
after running the generator). See `SECURITY_TEST_MATRIX.md`.
