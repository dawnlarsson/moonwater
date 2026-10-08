#!/usr/bin/env python3
"""Trace real loopback TLS, certificate, and downgrade paths into coverage."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="new directory for logs, retained binaries, and maps")
    parser.add_argument("--baseline", type=Path,
                        help="optional prior coverage directory to combine")
    parser.add_argument("--against", type=Path,
                        help="optional saved coverage JSON for delta reporting")
    parser.add_argument("--mutations", type=int, default=500)
    parser.add_argument("--schedules", type=int, default=60)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    if args.mutations < 1 or args.schedules < 1 or args.timeout < 60:
        parser.error("positive mutations/schedules and timeout >= 60 required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    coverage = output / "coverage"
    coverage.mkdir()
    if args.baseline:
        baseline = args.baseline.resolve()
        for elf in baseline.glob("*.elf"):
            record = elf.with_suffix(".map")
            if not record.is_file():
                continue
            stem = "baseline-" + elf.name
            shutil.copy2(elf, coverage / stem)
            shutil.copy2(record, (coverage / stem).with_suffix(".map"))

    rows = (
        ("tls-chains", ["--harness", "tls_chains"]),
        ("tls-mutations", ["--harness", "tls_peer", "--mutate", str(args.mutations)]),
        ("tls-schedules", ["--harness", "tls_peer", "--schedule", str(args.schedules)]),
        ("https-downgrade", ["--harness", "https_downgrade"]),
    )
    results = []
    for label, arguments in rows:
        environment = dict(os.environ, MOONWATER_INTEGRATION_COVERAGE=str(coverage),
                           MOONWATER_INTEGRATION_LABEL=label, PYTHONUNBUFFERED="1")
        began = time.monotonic()
        log = output / (label + ".log")
        print("network integration: " + label, flush=True)
        try:
            with log.open("w") as stream:
                done = subprocess.run([sys.executable, "test/differential.py", *arguments],
                                      cwd=ROOT, env=environment, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=args.timeout)
            code = done.returncode
            timed_out = False
        except subprocess.TimeoutExpired:
            code = 1
            timed_out = True
        results.append(dict(name=label, exit=code, timed_out=timed_out,
                            duration_seconds=round(time.monotonic() - began, 3),
                            log=str(log)))

    report = output / "coverage.json"
    command = [sys.executable, "test/differential.py", "--harness", "coverage_report",
               str(coverage), "--save", str(report), "--files", "src/net/"]
    if args.against:
        command += ["--against", str(args.against.resolve())]
    covered = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (output / "coverage.log").write_text(covered.stdout + covered.stderr)
    codes = [row["exit"] for row in results]
    overall = (1 if covered.returncode or any(code not in (0, 2) for code in codes)
               else 2 if 2 in codes else 0)
    summary = dict(schema="moonwater.network_integration.v1", results=results,
                   coverage_exit=covered.returncode, overall_exit=overall,
                   coverage_note="Trace-PC coverage includes retained loopback shell "
                                 "binaries and an optional copied baseline; it does not "
                                 "cover kernel or driver code.")
    (output / "integration.json").write_text(json.dumps(summary, indent=2) + "\n")
    print((output / "coverage.log").read_text(), end="")
    return overall


if __name__ == "__main__":
    sys.exit(main())
