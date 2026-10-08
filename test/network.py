#!/usr/bin/env python3
"""Repeatable local ASan/UBSan network campaigns with procedural boundaries."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="new directory for reports, logs, and replay corpora")
    parser.add_argument("--seeds", nargs="+", type=int, default=[1, 7, 42])
    parser.add_argument("--runs", type=int, default=200000)
    parser.add_argument("--seconds", type=int, default=30,
                        help="time budget per target (either run or time limit stops it)")
    args = parser.parse_args()
    if args.runs < 1 or args.seconds < 1 or any(
            not 1 <= seed <= 0xffffffff for seed in args.seeds):
        parser.error("positive runs/seconds and seeds in 1..4294967295 required")
    if len(set(args.seeds)) != len(args.seeds):
        parser.error("seeds must be distinct")
    output = args.output.resolve()
    # A new directory prevents accidentally reporting stale campaign artifacts.
    output.mkdir(parents=True, exist_ok=False)
    reports = []
    for seed in args.seeds:
        run = output / ("seed-%d" % seed)
        run.mkdir()
        environment = dict(os.environ, PYTHONUNBUFFERED="1",
                           MOONWATER_MSAN="0",
                           MOONWATER_FUZZ_BOUNDARIES="1",
                           MOONWATER_FUZZ_SEED=str(seed),
                           MOONWATER_FUZZ_RUNS=str(args.runs),
                           MOONWATER_FUZZ_SECONDS=str(args.seconds),
                           MOONWATER_FUZZ_REPORT=str(run / "report.json"),
                           MOONWATER_FUZZ_ARTIFACTS=str(run / "artifacts"))
        print("network campaign seed %d: %s" % (seed, run), flush=True)
        with (run / "campaign.log").open("w") as log:
            done = subprocess.run([sys.executable, "test/differential.py",
                                   "--harness", "tls_fuzz"], cwd=ROOT,
                                  env=environment, stdout=log, stderr=subprocess.STDOUT)
        entry = dict(seed=seed, exit=done.returncode, report=str(run / "report.json"))
        try:
            report = json.loads((run / "report.json").read_text())
            invocations = [inv for target in report["targets"]
                           for inv in target.get("invocations", [])]
            entry["invocations"] = invocations
            # Clean exit without evidence of execution must not count as coverage.
            if done.returncode == 0 and (len(report["targets"]) != 14 or
                    len(invocations) != 15 or any(
                        inv.get("exit") != 0 or
                        inv.get("metrics", {}).get("executed_units", 0) == 0 or
                        inv.get("corpus", {}).get("generated_seeds", 0) == 0
                        for inv in invocations)):
                entry.update(exit=1, error="missing target execution or procedural evidence")
        except (OSError, ValueError, KeyError) as error:
            entry.update(exit=1, error="missing/invalid report: " + str(error))
        reports.append(entry)
        print("seed %d completed: exit %d" % (seed, entry["exit"]), flush=True)
    codes = [report["exit"] for report in reports]
    overall = 1 if any(code not in (0, 2) for code in codes) else (2 if 2 in codes else 0)
    (output / "campaign.json").write_text(json.dumps(dict(
        schema="moonwater.network_campaign.v1", runs=args.runs,
        seconds=args.seconds, campaigns=reports, overall_exit=overall,
        coverage_note="Counters cover hosted parser lifts. Kernel, drivers, live "
                      "network integration, and all protocol states require separate tests."
    ), indent=2) + "\n")
    return overall


if __name__ == "__main__":
    sys.exit(main())
