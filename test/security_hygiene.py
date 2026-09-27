#!/usr/bin/env python3
"""Procedural hygiene for the security / net test surface.

Catches classes of slop that have already bitten this tree, and a widening
set of structural / future test-suite bugs:

* ``check()`` names that are not string literals inside ``CHECK_net``
  (the macro concatenates ``"  FAIL " name`` — ternaries do not compile)
* tracked ``*.bin`` under ``test/fuzz_corpus/`` (hex lives in
  ``generate_seeds.py``)
* stale "attach the corpus tree" wording that ignores the hex source
* lane / fuzz_net harness names missing from ``differential.py`` registry
* ``lane_net`` / ``lane_tar`` soft-skip (exit 2) wiring for sanitizer /
  race harnesses; both lanes must invoke this script
* seed generator materializes; non-``empty.bin`` seeds are non-empty;
  on-disk count matches SEEDS
* ``fuzz_net`` seed counting must use ``*.bin`` only
* ``http_response_framing`` CASES unique; MUST_ACCEPT ⊆ CASES; DELIBERATE
  rows must keep a must-disagree assert against http.client
* ``tls_verify_fuzz`` driver must call production ``tls_verify_one`` (not
  mock-refuse-only), keep WR2/GTS prove, and use the hosted crypto lift
* CHECK_net must keep WR2→GTS accept + flipped-signature refuse proves
* matrix / checklist / fuzz_net must not claim mocked signatures
* SECURITY_TEST_MATRIX backticks that look like harness names must be
  registered (or explicitly documented as non-harness)
* harness registry keys unique; required soft-skip harnesses have an
  exit-2 NOT RUN path
* ``https_downgrade`` keeps multiple Location shapes
* generate_seeds hex values are even-length hex only
Run:

    python3 test/security_hygiene.py

Wired at the start of ``lane_net`` / ``lane_tar``. Exit 0 clean, 1 fail.
"""

from __future__ import annotations

import importlib.util
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKS = ROOT / "test" / "checks.c"
DIFFERENTIAL = ROOT / "test" / "differential.py"
RUN = ROOT / "test" / "run"
FUZZ_NET = ROOT / "test" / "fuzz_net"
GENERATE = ROOT / "test" / "fuzz_corpus" / "generate_seeds.py"
HOSTED = ROOT / "test" / "tls_verify_hosted.py"
DOC_PATHS = (
    ROOT / "SECURITY_CHECKLIST.md",
    ROOT / "SECURITY_TEST_MATRIX.md",
    FUZZ_NET,
    ROOT / "test" / "fuzz_campaign",
    ROOT / "test" / "fuzz_corpus" / "README.md",
)

# Phrases that mean "commit / attach raw corpus blobs" after the hex move.
STALE_ATTACH = (
    "attach this file and test/fuzz_corpus/ (or a tarball of it)",
    "plus `test/fuzz_corpus/` (or a tarball of it)",
    "Attach that report plus `test/fuzz_corpus/`",
)

MOCKED_SIG_PHRASES = (
    "signatures mocked",
    "mocked signature",
    "mocked refuse",
    "signatures always refuse",
)


def fail(failures: list[str], message: str) -> None:
    failures.append(message)


def check_net_literal_names(failures: list[str]) -> None:
    text = CHECKS.read_text()
    start = text.find("#ifdef CHECK_net")
    if start < 0:
        fail(failures, "CHECK_net block missing from test/checks.c")
        return
    end = re.search(r"\n#ifdef CHECK_", text[start + 1 :])
    region = text[start : (start + 1 + end.start()) if end else len(text)]
    base_line = text.count("\n", 0, start) + 1

    for m in re.finditer(r"\bcheck\s*\(\s*([^,\n]+)\s*,", region):
        arg = m.group(1).strip()
        line = base_line + region.count("\n", 0, m.start())
        if arg.startswith('"'):
            continue
        if "?" in arg:
            fail(
                failures,
                f"test/checks.c:{line}: CHECK_net check() name is not a "
                f"string literal (ternary?): {arg[:80]}",
            )
            continue
        fail(
            failures,
            f"test/checks.c:{line}: CHECK_net check() name must be a "
            f'string literal, got: {arg[:80]}',
        )


def check_no_tracked_bins(failures: list[str]) -> None:
    listed = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "test/fuzz_corpus"],
        capture_output=True,
        text=True,
        check=False,
    )
    if listed.returncode != 0:
        fail(failures, "git ls-files test/fuzz_corpus failed")
        return
    bins = [
        line
        for line in listed.stdout.splitlines()
        if line.endswith(".bin")
    ]
    if bins:
        fail(
            failures,
            "tracked fuzz corpus *.bin files (use generate_seeds.py hex): "
            + ", ".join(bins[:8])
            + ("…" if len(bins) > 8 else ""),
        )


def check_stale_attach_wording(failures: list[str]) -> None:
    for path in DOC_PATHS:
        if not path.is_file():
            continue
        text = path.read_text()
        lower = text.lower()
        for phrase in STALE_ATTACH:
            if phrase.lower() in lower:
                fail(
                    failures,
                    f"{path.relative_to(ROOT)}: stale attach wording "
                    f"(want generate_seeds.py): {phrase!r}",
                )
        for m in re.finditer(
            r"attach[^\n]{0,80}test/fuzz_corpus/(?!generate_seeds)",
            text,
            re.I,
        ):
            window = text[max(0, m.start() - 40) : m.end() + 40]
            if "generate_seeds" in window or "materializ" in window.lower():
                continue
            fail(
                failures,
                f"{path.relative_to(ROOT)}: attach path mentions "
                f"test/fuzz_corpus/ without generate_seeds.py nearby",
            )


def registered_harnesses() -> set[str]:
    text = DIFFERENTIAL.read_text()
    return set(re.findall(r'^\s*"([A-Za-z0-9_]+)":\s*harness_', text, re.M))


def check_harness_wiring(failures: list[str]) -> None:
    registered = registered_harnesses()
    required = {
        "tls_chains",
        "https_downgrade",
        "http_response_framing",
        "tls_der_fuzz",
        "tls_hs_fuzz",
        "tls_verify_fuzz",
        "msan_net",
        "pathname_race",
    }
    missing = sorted(required - registered)
    if missing:
        fail(
            failures,
            "differential.py missing harness registry entries: "
            + ", ".join(missing),
        )

    # Duplicate registry keys (last-wins bugs).
    text = DIFFERENTIAL.read_text()
    keys = re.findall(r'^\s*"([A-Za-z0-9_]+)":\s*harness_', text, re.M)
    seen: dict[str, int] = {}
    for key in keys:
        seen[key] = seen.get(key, 0) + 1
    dupes = sorted(k for k, n in seen.items() if n > 1)
    if dupes:
        fail(
            failures,
            "differential.py duplicate harness registry keys: "
            + ", ".join(dupes[:8]),
        )

    run = RUN.read_text()
    for name in (
        "tls_der_fuzz",
        "tls_hs_fuzz",
        "https_downgrade",
        "http_response_framing",
        "tls_chains",
        "pathname_race",
    ):
        if f"--harness {name}" not in run:
            fail(failures, f"test/run: lane does not invoke --harness {name}")

    if not re.search(r"tls_der_fuzz: skipped \(soft\)", run):
        fail(failures, "test/run: lane_net missing soft skip for tls_der_fuzz")
    if not re.search(r"tls_hs_fuzz: skipped \(soft\)", run):
        fail(failures, "test/run: lane_net missing soft skip for tls_hs_fuzz")
    if not re.search(r"pathname_race: skipped \(soft\)", run):
        fail(failures, "test/run: lane_tar missing soft skip for pathname_race")

    for lane in ("lane_net", "lane_tar"):
        block = re.search(
            rf"{lane}\(\)\s*\n\{{(?P<body>.*?)\n\}}",
            run,
            re.S,
        )
        if not block:
            fail(failures, f"test/run: cannot find {lane}() body")
            continue
        if "security_hygiene.py" not in block.group("body"):
            fail(
                failures,
                f"test/run: {lane}() must invoke test/security_hygiene.py",
            )

    fuzz_net = FUZZ_NET.read_text()
    for name in ("tls_der_fuzz", "tls_hs_fuzz", "tls_verify_fuzz"):
        if name not in fuzz_net:
            fail(failures, f"test/fuzz_net: does not run {name}")

    if "-name '*.bin'" not in fuzz_net and '-name "*.bin"' not in fuzz_net:
        fail(
            failures,
            "test/fuzz_net: count_seeds must restrict to *.bin "
            "(stray non-seed files must not inflate the report)",
        )


def check_seed_generator(failures: list[str]) -> None:
    if not GENERATE.is_file():
        fail(failures, "missing test/fuzz_corpus/generate_seeds.py")
        return
    spec = importlib.util.spec_from_file_location(
        "moonwater_fuzz_generate_seeds_hygiene", GENERATE
    )
    if spec is None or spec.loader is None:
        fail(failures, "cannot load generate_seeds.py")
        return
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.materialize()
    seeds = module.SEEDS
    for sub in ("tls_der", "tls_hs"):
        if sub not in seeds or not seeds[sub]:
            fail(failures, f"generate_seeds.py: empty SEEDS[{sub!r}]")
            continue
        directory = ROOT / "test" / "fuzz_corpus" / sub
        on_disk = sorted(p.name for p in directory.glob("*.bin"))
        expected = sorted(seeds[sub])
        if on_disk != expected:
            fail(
                failures,
                f"generate_seeds.py: on-disk *.bin for {sub} != SEEDS "
                f"(disk {len(on_disk)} vs seeds {len(expected)})",
            )
        for name, hx in seeds[sub].items():
            if not name.endswith(".bin"):
                fail(
                    failures,
                    f"generate_seeds.py: seed name must end in .bin: {name}",
                )
            if not re.fullmatch(r"(?:[0-9a-fA-F]{2})*", hx):
                fail(
                    failures,
                    f"generate_seeds.py: {sub}/{name} hex is not even-length "
                    f"hexadecimal",
                )
                continue
            data = bytes.fromhex(hx)
            path = directory / name
            if not path.is_file():
                fail(failures, f"seed not materialized: {path.relative_to(ROOT)}")
                continue
            if path.read_bytes() != data:
                fail(
                    failures,
                    f"seed drift vs hex: {path.relative_to(ROOT)}",
                )
            if name != "empty.bin" and len(data) == 0:
                fail(
                    failures,
                    f"empty hostile seed (use empty.bin for the empty control): "
                    f"{sub}/{name}",
                )


def _harness_body(name: str) -> str | None:
    text = DIFFERENTIAL.read_text()
    pattern = (
        rf"def harness_{name}\(argv\):(.*?)(?=\ndef harness_|\ndef [a-z]|\Z)"
    )
    match = re.search(pattern, text, re.S)
    return match.group(1) if match else None


def check_tls_verify_fuzz_real_sig(failures: list[str]) -> None:
    body = _harness_body("tls_verify_fuzz")
    if body is None:
        fail(failures, "differential.py: missing harness_tls_verify_fuzz")
        return
    if "tls_verify_one(" not in body:
        fail(
            failures,
            "tls_verify_fuzz: driver must call production tls_verify_one "
            "(signature path must not be mock-refuse-only)",
        )
    if "fuzz_prove_wr2_gts" not in body and "WR2" not in body:
        fail(
            failures,
            "tls_verify_fuzz: must prove WR2→GTS tls_verify_one before fuzzing",
        )
    if "build_tls_verify_fuzz_source" not in body and "tls_verify_hosted" not in body:
        fail(
            failures,
            "tls_verify_fuzz: must build via test/tls_verify_hosted.py lift",
        )
    if re.search(
        r"/\*\s*Mocked signature refuse[\s\S]{0,120}return false;",
        body,
    ):
        fail(
            failures,
            "tls_verify_fuzz: still contains mocked signature refuse path",
        )
    if not HOSTED.is_file():
        fail(failures, "missing test/tls_verify_hosted.py")
        return
    hosted = HOSTED.read_text()
    for needle in (
        "montgomery_multiply",
        "force_c_field_ops",
        "crypto_sha256_of",
        "tls_verify_one",
    ):
        if needle not in hosted:
            fail(
                failures,
                f"tls_verify_hosted.py: missing {needle} (hosted crypto lift)",
            )


def check_check_net_verify_proofs(failures: list[str]) -> None:
    text = CHECKS.read_text()
    start = text.find("#ifdef CHECK_net")
    if start < 0:
        fail(failures, "CHECK_net block missing from test/checks.c")
        return
    end = re.search(r"\n#ifdef CHECK_", text[start + 1 :])
    region = text[start : (start + 1 + end.start()) if end else len(text)]
    need = (
        'check("WR2 chains to the served GTS Root R1"',
        "tls_verify_one(address_of wr2_cert, address_of gts_cert)",
        'check("the SHA-384 PKCS#1 verify refuses one flipped signature bit"',
    )
    for needle in need:
        if needle not in region:
            fail(
                failures,
                "CHECK_net: missing freestanding verify proof: " + needle[:60],
            )


def check_no_mocked_sig_docs(failures: list[str]) -> None:
    for path in DOC_PATHS:
        if not path.is_file():
            continue
        text = path.read_text()
        lower = text.lower()
        # Only flag when tls_verify_fuzz is in the same paragraph/window.
        for phrase in MOCKED_SIG_PHRASES:
            idx = 0
            while True:
                at = lower.find(phrase, idx)
                if at < 0:
                    break
                window = lower[max(0, at - 120) : at + len(phrase) + 120]
                if "tls_verify" in window or "verify_fuzz" in window:
                    fail(
                        failures,
                        f"{path.relative_to(ROOT)}: still claims {phrase!r} "
                        f"near tls_verify_fuzz",
                    )
                idx = at + len(phrase)


def check_matrix_harness_names(failures: list[str]) -> None:
    matrix = ROOT / "SECURITY_TEST_MATRIX.md"
    if not matrix.is_file():
        return
    registered = registered_harnesses()
    text = matrix.read_text()
    # Backticked snake_case tokens ending in _fuzz / known harnesses.
    candidates = set(re.findall(r"`([a-z][a-z0-9_]*(?:_fuzz|_race|_net|_chains|_downgrade|_framing))`", text))
    # Allow a few non-registry documentation tokens.
    allow = {
        "lane_net",
        "fuzz_net",
        "msan_net",  # registered
    }
    unknown = sorted(
        c for c in candidates if c not in registered and c not in allow
        and not c.startswith("lane_")
    )
    # Filter to likely harness names (must exist as harness_*)
    real_unknown = []
    diff = DIFFERENTIAL.read_text()
    for name in unknown:
        if f"def harness_{name}(" in diff:
            # defined but not registered
            real_unknown.append(name + " (unregistered)")
        elif name.endswith("_fuzz") or name in {
            "pathname_race",
            "https_downgrade",
            "http_response_framing",
            "tls_chains",
        }:
            real_unknown.append(name)
    if real_unknown:
        fail(
            failures,
            "SECURITY_TEST_MATRIX.md harness-like names not registered: "
            + ", ".join(real_unknown[:10]),
        )


def check_soft_skip_paths(failures: list[str]) -> None:
    for name in ("tls_der_fuzz", "tls_hs_fuzz", "tls_verify_fuzz", "msan_net"):
        body = _harness_body(name)
        if body is None:
            continue
        if "return 2" not in body and "NOT RUN" not in body:
            fail(
                failures,
                f"{name}: expected a soft NOT RUN / return 2 path when "
                f"tools are missing",
            )


def check_http_framing_matrix(failures: list[str]) -> None:
    body = _harness_body("http_response_framing")
    if body is None:
        fail(failures, "differential.py: missing harness_http_response_framing")
        return
    cases_match = re.search(r"CASES\s*=\s*\[(.*?)\n\s*\]", body, re.S)
    if not cases_match:
        fail(failures, "http_response_framing: cannot find CASES list")
        return
    names = re.findall(r'\(\s*"([^"]+)"\s*,', cases_match.group(1))
    if len(names) != len(set(names)):
        fail(failures, "http_response_framing: duplicate CASES names")
    accept_match = re.search(r"MUST_ACCEPT\s*=\s*\{(.*?)\n\s*\}", body, re.S)
    if not accept_match:
        fail(failures, "http_response_framing: cannot find MUST_ACCEPT")
        return
    accept = set(re.findall(r'"([^"]+)"', accept_match.group(1)))
    missing = sorted(accept - set(names))
    if missing:
        fail(
            failures,
            "http_response_framing: MUST_ACCEPT not in CASES: "
            + ", ".join(missing[:8]),
        )
    deliberate_match = re.search(r"DELIBERATE\s*=\s*\{(.*?)\n\s*\}", body, re.S)
    if not deliberate_match:
        fail(failures, "http_response_framing: cannot find DELIBERATE")
        return
    deliberate = set(re.findall(r'"([^"]+)"\s*:', deliberate_match.group(1)))
    if "http.client now agrees" not in body and \
            "listed deliberate but http.client now agrees" not in body:
        fail(
            failures,
            "http_response_framing: DELIBERATE must assert http.client "
            "still disagrees (vacuous checks(True) without disagree gate)",
        )
    unknown = sorted(deliberate - set(names))
    if unknown:
        fail(
            failures,
            "http_response_framing: DELIBERATE keys not in CASES: "
            + ", ".join(unknown[:8]),
        )


def check_https_downgrade_depth(failures: list[str]) -> None:
    body = _harness_body("https_downgrade")
    if body is None:
        fail(failures, "differential.py: missing harness_https_downgrade")
        return
    shapes = 0
    for needle in (
        "http://",
        "HTTP://",
        "//",
        "user@",
        "http:///",
    ):
        if needle in body:
            shapes += 1
    if shapes < 4:
        fail(
            failures,
            "https_downgrade: expected multiple Location shapes "
            f"(found {shapes} markers); matrix claims expanded coverage",
        )


def main() -> int:
    failures: list[str] = []
    check_net_literal_names(failures)
    check_no_tracked_bins(failures)
    check_stale_attach_wording(failures)
    check_harness_wiring(failures)
    check_seed_generator(failures)
    check_tls_verify_fuzz_real_sig(failures)
    check_check_net_verify_proofs(failures)
    check_no_mocked_sig_docs(failures)
    check_matrix_harness_names(failures)
    check_soft_skip_paths(failures)
    check_http_framing_matrix(failures)
    check_https_downgrade_depth(failures)

    if failures:
        print("security hygiene: FAIL")
        for item in failures:
            print("  - " + item)
        print(f"security hygiene 0/{len(failures)}")
        return 1
    print("security hygiene: ok")
    print("security hygiene 1/1")
    return 0


if __name__ == "__main__":
    sys.exit(main())
