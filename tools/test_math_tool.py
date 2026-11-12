#!/usr/bin/env python3
"""Integration checks for math-tool CLI bounds and reproducible output contracts."""

import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "tools" / ".build" / "roulette_math"


class MathToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "tools" / "run_math.py"), "--build-only"], check=True)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="roulette-math-test-")
        self.directory = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_tool(self, *args):
        return subprocess.run([str(BINARY), *map(str, args)], text=True, capture_output=True)

    def test_invalid_arguments_fail_before_writing(self):
        invalid = [
            ["--spins", "0"], ["--spins", "1"], ["--spins", "-1"],
            ["--spins", "1000000001"], ["--spins", "100junk"],
            ["--spins", "18446744073709551616"], ["--spins", "2.5"],
            ["--seed", "4294967296"], ["--seed", "-1"], ["--seed", "+1"],
            ["--seed", "1", "--seed", "2"], ["--unknown", "2"],
        ]
        for args in invalid:
            with self.subTest(args=args):
                destination = self.directory / "never-created"
                result = self.run_tool(*args, "--output", destination)
                self.assertEqual(2, result.returncode, result.stderr)
                self.assertFalse(destination.exists())
        result = self.run_tool("--spins")
        self.assertEqual(2, result.returncode)

    def test_seeded_outputs_and_accounting_are_reproducible(self):
        first, second = self.directory / "first", self.directory / "second"
        for destination in (first, second):
            result = self.run_tool("--spins", "200", "--seed", "1234567", "--output", destination)
            self.assertEqual(0, result.returncode, result.stderr)
        for filename in ("RESULTS.md", "histogram.csv", "convergence.csv"):
            self.assertEqual((first / filename).read_bytes(), (second / filename).read_bytes())
        with (first / "histogram.csv").open(newline="") as source:
            histogram = list(csv.DictReader(source))
        self.assertEqual(list(range(37)), [int(row["number"]) for row in histogram])
        self.assertEqual(200, sum(int(row["observed"]) for row in histogram))
        for row in histogram:
            self.assertAlmostEqual(200 / 37, float(row["expected"]), places=9)
        with (first / "convergence.csv").open(newline="") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(1, len(rows))
        row = rows[0]
        self.assertEqual(200, int(row["spins"]))
        zero_count = int(histogram[0]["observed"])
        self.assertEqual(36 * zero_count, int(row["standard_returned"]))
        self.assertLessEqual(30 * zero_count, int(row["lucky_returned"]))
        self.assertGreaterEqual(500 * zero_count, int(row["lucky_returned"]))
        self.assertLessEqual(30 * 200, int(row["portfolio_returned"]))
        self.assertGreaterEqual(500 * 200, int(row["portfolio_returned"]))
        self.assertAlmostEqual(int(row["lucky_returned"]) / 200, float(row["lucky_rtp"]), places=9)
        self.assertAlmostEqual(int(row["portfolio_returned"]) / (37 * 200), float(row["portfolio_rtp"]), places=9)
        self.assertIn("**Smoke run:**", (first / "RESULTS.md").read_text())

    def test_convergence_keeps_final_partial_interval(self):
        destination = self.directory / "convergence"
        result = self.run_tool("--spins", "100001", "--seed", "4294967295", "--output", destination)
        self.assertEqual(0, result.returncode, result.stderr)
        with (destination / "convergence.csv").open(newline="") as source:
            rows = list(csv.DictReader(source))
        self.assertEqual([100000, 100001], [int(row["spins"]) for row in rows])
        for field in ("standard_returned", "lucky_returned", "portfolio_returned"):
            self.assertGreaterEqual(int(rows[1][field]), int(rows[0][field]))

    def test_published_experiment_reproduces_with_the_recorded_seed(self):
        result = self.run_tool("--spins", "10000000", "--seed", "20261111", "--output", self.directory)
        self.assertEqual(0, result.returncode, result.stderr)
        published = ROOT / "tools" / "results"
        # The report includes the observed returns and confidence intervals.
        # Compare the recorded experiment even when an interval misses theory.
        for name in ("RESULTS.md", "convergence.csv", "histogram.csv"):
            with self.subTest(output=name):
                self.assertEqual((published / name).read_text(), (self.directory / name).read_text())

    def test_invalid_output_path_fails_without_destroying_file(self):
        destination = self.directory / "existing-file"
        destination.write_text("keep me")
        result = self.run_tool("--spins", "200", "--output", destination)
        self.assertEqual(1, result.returncode)
        self.assertEqual("keep me", destination.read_text())


if __name__ == "__main__":
    unittest.main()
