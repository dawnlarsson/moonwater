#!/usr/bin/env python3
"""Procedural hygiene for the security / net test surface.

Catches classes of slop that have already bitten this tree:

* ``check()`` names that are not string literals inside ``CHECK_net``
  (the macro concatenates ``"  FAIL " name`` — ternaries do not compile)
* tracked ``*.bin`` under ``test/fuzz_corpus/`` (hex lives in
  ``generate_seeds.py``)
* stale "attach the corpus tree" wording that ignores the hex source
* lane / fuzz_net harness names missing from ``differential.py`` registry
* ``lane_net`` / ``lane_tar`` soft-skip (exit 2) wiring for sanitizer /
  race harnesses
* seed generator materializes; non-``empty.bin`` seeds are non-empty

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


def fail(failures: list[str], message: str) -> None:
    failures.append(message)


def check_net_literal_names(failures: list[str]) -> None:
    text = CHECKS.read_text()
    start = text.find("#ifdef CHECK_net")
    if start < 0:
        fail(failures, "CHECK_net block missing from test/checks.c")
        return
    # End at next top-level #ifdef CHECK_ or EOF-ish sibling.
    end = re.search(r"\n#ifdef CHECK_", text[start + 1 :])
    region = text[start : (start + 1 + end.start()) if end else len(text)]
    base_line = text.count("\n", 0, start) + 1

    for m in re.finditer(r"\bcheck\s*\(\s*([^,\n]+)\s*,", region):
        arg = m.group(1).strip()
        line = base_line + region.count("\n", 0, m.start())
        # String literal or adjacent-literal concat starting with ".
        if arg.startswith('"'):
            continue
        # Ternary names (the bug that broke the tree).
        if "?" in arg:
            fail(
                failures,
                f"test/checks.c:{line}: CHECK_net check() name is not a "
                f"string literal (ternary?): {arg[:80]}",
            )
            continue
        # Macro-parameter paste like label ": …" is OK outside CHECK_net
        # (see lock_states); inside CHECK_net require a real literal.
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
        # Broader: "attach … test/fuzz_corpus/" without generate_seeds nearby
        for m in re.finditer(
            r"attach[^\n]{0,80}test/fuzz_corpus/(?!generate_seeds)",
            text,
            re.I,
        ):
            # Allow "materialized … test/fuzz_corpus/tls_der"
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

    run = RUN.read_text()
    for name in (
        "tls_der_fuzz",
        "tls_hs_fuzz",
        "https_downgrade",
        "http_response_framing",
        "tls_chains",
        "pathname_race",
    ):
        if f"--harness {name}" not in run and f"harness {name}" not in run:
            # lane uses --harness name
            if f"--harness {name}" not in run:
                fail(failures, f"test/run: lane does not invoke --harness {name}")

    # Soft-skip exit 2 must not poison net for der/hs; must soft-skip race.
    if not re.search(
        r"tls_der_fuzz: skipped \(soft\)",
        run,
    ):
        fail(failures, "test/run: lane_net missing soft skip for tls_der_fuzz")
    if not re.search(
        r"tls_hs_fuzz: skipped \(soft\)",
        run,
    ):
        fail(failures, "test/run: lane_net missing soft skip for tls_hs_fuzz")
    if not re.search(
        r"pathname_race: skipped \(soft\)",
        run,
    ):
        fail(failures, "test/run: lane_tar missing soft skip for pathname_race")

    fuzz_net = FUZZ_NET.read_text()
    for name in ("tls_der_fuzz", "tls_hs_fuzz", "tls_verify_fuzz"):
        if name not in fuzz_net:
            fail(failures, f"test/fuzz_net: does not run {name}")


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
        for name, hx in seeds[sub].items():
            if not name.endswith(".bin"):
                fail(
                    failures,
                    f"generate_seeds.py: seed name must end in .bin: {name}",
                )
            data = bytes.fromhex(hx)
            path = ROOT / "test" / "fuzz_corpus" / sub / name
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


def main() -> int:
    failures: list[str] = []
    check_net_literal_names(failures)
    check_no_tracked_bins(failures)
    check_stale_attach_wording(failures)
    check_harness_wiring(failures)
    check_seed_generator(failures)

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
