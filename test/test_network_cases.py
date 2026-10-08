"""Generator guarantees and libFuzzer reporting, independent of parser results."""
import unittest
from unittest import mock
import subprocess
import contextlib
import io
import json
from pathlib import Path
import tempfile

from network_cases import expand, variants
import differential
import network
from differential import tls_fuzz_metrics


class NetworkCases(unittest.TestCase):
    def test_controls_determinism_and_uniqueness(self):
        seeds = {"valid": b"abcdefgh", "empty": b""}
        first, census = expand(seeds, 32, limit=200)
        self.assertEqual((first, census), expand(seeds, 32, limit=200))
        for name, data in seeds.items():
            self.assertEqual(first[name], data)
        self.assertEqual(len(set(first.values())), len(first))
        self.assertEqual(len(first) - len(seeds), census["generated_seeds"])

    def test_caps_and_round_robin(self):
        seeds = {"a": b"abcdefgh", "b": b"12345678"}
        cases, census = expand(seeds, 32, limit=2)
        self.assertTrue(census["capped"])
        self.assertEqual(census["generated_seeds"], 2)
        self.assertIn(b"abcdefg", cases.values())
        self.assertIn(b"1234567", cases.values())
        cases, census = expand(seeds, 32, byte_limit=6)
        self.assertEqual(cases, seeds)
        self.assertTrue(census["capped"])

    def test_lengths_truncations_and_integer_boundaries(self):
        data = bytes(range(32))
        cases = set(variants(data, 40))
        self.assertTrue(all(len(case) <= 40 for case in cases))
        for cut in range(32):
            self.assertIn(data[:cut], cases)
        for replacement in (b"\xff\xff", b"\x7f\x00", b"\x00\x7f",
                            b"\xff\xff\xff\xff", b"\x00" * 4):
            self.assertIn(replacement + data[len(replacement):], cases)
        self.assertIn(data + b"\x00", cases)

    def test_mutation_classes_survive_small_budget(self):
        data = b"abcdefgh"
        cases, _ = expand({"valid": data}, 32, limit=16)
        self.assertIn(data[:-1], cases.values())
        self.assertIn(data + b"\x00", cases.values())
        for width in (1, 2, 4, 8):
            self.assertIn(b"\xff" * width + data[width:], cases.values())

    def test_empty_oversized_and_name_collision(self):
        seeds = {"procedural_000001.bin": b"abcd", "empty": b""}
        cases, census = expand(seeds, 2, limit=100)
        self.assertEqual(cases["procedural_000001.bin"], b"abcd")
        self.assertTrue(all(len(data) <= 2 for name, data in cases.items()
                            if name not in seeds))
        self.assertFalse(census["capped"])

    def test_metrics_use_final_counters_and_do_not_invent_missing_data(self):
        log = ("INFO: Loaded 1 PC tables (100 PCs):\n"
               "#10 INITED cov: 20 ft: 25\n#200 DONE cov: 40 ft: 55\n"
               "stat::number_of_executed_units: 200\nstat::peak_rss_mb: 45\n")
        self.assertEqual(tls_fuzz_metrics(log), dict(executed_units=200,
            initialized_units=10, mutation_units=190, peak_rss_mb=45,
            covered_edges=40, features=55, instrumented_pcs=100))
        self.assertEqual(tls_fuzz_metrics("NOT RUN"), {})

    def test_script_subtest_distinguishes_missing_tool_and_broken_lift(self):
        with mock.patch.object(differential.shutil, "which", return_value=None), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(differential.waterlink_script_scan(""), 2)
        for probe_exit, expected in ((1, 2), (0, 1)):
            results = [subprocess.CompletedProcess([], probe_exit, "", "probe")]
            if probe_exit == 0:
                results.append(subprocess.CompletedProcess([], 1, "", "broken lift"))
            with mock.patch.object(differential.shutil, "which", return_value="clang"), \
                    mock.patch.object(differential.subprocess, "run", side_effect=results), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(differential.waterlink_script_scan(""), expected)

    def test_waterlink_propagates_required_subtest_skip(self):
        with mock.patch.object(differential, "harness_waterlink_sanitized",
                               return_value=(None, "")), \
                mock.patch.object(differential, "waterlink_script_scan", return_value=2):
            self.assertEqual(differential.harness_waterlink_fuzz([]), 2)

    def test_campaign_requires_execution_and_pins_asan_ubsan(self):
        for exit_code, expected in ((0, 1), (2, 2)):
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "campaign"

                def fake_run(command, **options):
                    environment = options["env"]
                    self.assertEqual(environment["MOONWATER_MSAN"], "0")
                    self.assertEqual(environment["MOONWATER_FUZZ_BOUNDARIES"], "1")
                    Path(environment["MOONWATER_FUZZ_REPORT"]).write_text(
                        json.dumps({"targets": []}))
                    return subprocess.CompletedProcess(command, exit_code)

                with mock.patch.object(network.sys, "argv", ["network", "--output",
                        str(output), "--seeds", "1", "--runs", "10", "--seconds", "1"]), \
                        mock.patch.dict(network.os.environ, {"MOONWATER_MSAN": "1"}), \
                        mock.patch.object(network.subprocess, "run", side_effect=fake_run), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(network.main(), expected)
                report = json.loads((output / "campaign.json").read_text())
                self.assertEqual(report["overall_exit"], expected)

    def test_integration_shell_is_retained_with_its_coverage_map(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "work" / "shell"
            output.parent.mkdir()
            with mock.patch.dict(differential.os.environ, {
                    "MOONWATER_INTEGRATION_COVERAGE": str(root / "coverage"),
                    "MOONWATER_INTEGRATION_LABEL": "tls test"}), \
                    mock.patch.object(differential.platform, "machine",
                                      return_value="x86_64"), \
                    mock.patch.object(differential.subprocess, "run",
                                      return_value=subprocess.CompletedProcess([], 0, "", "")):
                command = differential.spark_shell_command("cc", output)
            retained = Path(command[command.index("-o") + 1])
            self.assertTrue(output.is_symlink())
            self.assertEqual(output.resolve(), retained)
            self.assertTrue(retained.with_suffix(".map").is_file())
            self.assertEqual(retained.with_suffix(".map").stat().st_size, 64 * 1024 * 1024)
            self.assertIn("-fsanitize-coverage=trace-pc", command)
            self.assertTrue(retained.with_suffix(".o").as_posix() in command)


if __name__ == "__main__":
    unittest.main()
