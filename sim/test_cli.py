#!/usr/bin/env python3
"""End-to-end checks for CLI arguments, deterministic rounds and printed accounting."""

from pathlib import Path
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "sim" / ".build" / "roulette_sim"
WHEEL_ORDER = [
    0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
    5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26,
]
GROSS_ODDS = {
    "Straight": 36, "Split": 18, "Street": 12, "Corner": 9,
    "SixLine": 6, "Dozen": 3, "Column": 3, "Red": 2, "Black": 2,
    "Odd": 2, "Even": 2, "Low": 2, "High": 2,
}
ROUND_HEADER = re.compile(r"^ROUND (\d+) \| events #(\d+)\.\.#(\d+)$", re.MULTILINE)
SEAT_LINE = re.compile(
    r"^  seat (\d+) \(([^)]+)\): (.*?) stake=(\d+) gross=(\d+) "
    r"refund=(\d+) net=(-?\d+) credits=(\d+)$", re.MULTILINE
)


class SimulatorCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run([sys.executable, str(ROOT / "sim" / "run.py"), "--build-only"], check=True)

    def run_cli(self, *arguments):
        return subprocess.run([str(BINARY), *map(str, arguments)], text=True, capture_output=True, timeout=60)

    def successful(self, *arguments):
        result = self.run_cli(*arguments)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("", result.stderr)
        return result.stdout

    def result_sequence(self, text):
        return [(int(number), int(pocket)) for number, pocket in re.findall(
            r"^  result: number=(\d+) pocket=(\d+)$", text, re.MULTILINE
        )]

    def verify_transcript(self, text, rounds, lucky, injected_fault=None):
        """Use printed selections and a separate payout oracle to check every credit."""
        headers = list(ROUND_HEADER.finditer(text))
        self.assertEqual(rounds, len(headers))
        credits = [1000] * 8
        played = [0] * 8
        won = [0] * 8
        house = 0
        sequence = 9  # Initialization and eight initial-credit events.
        skipped = 0
        settled = 0
        voided = 0
        for index, header in enumerate(headers):
            self.assertEqual(index + 1, int(header[1]))
            self.assertEqual(sequence + 1, int(header[2]))
            last_sequence = int(header[3])
            self.assertGreaterEqual(last_sequence, int(header[2]))
            end = headers[index + 1].start() if index + 1 < len(headers) else text.index("FINAL METERS")
            body = text[header.end():end]
            states_match = re.search(r"^  states: (.+)$", body, re.MULTILINE)
            self.assertIsNotNone(states_match)
            states = states_match[1].split(" > ")
            lucky_match = re.search(r"^  lucky:(.*)$", body, re.MULTILINE)
            self.assertIsNotNone(lucky_match)
            multipliers = {}
            if lucky:
                entries = re.findall(r"(\d+)@(\d+)x", lucky_match[1])
                self.assertGreaterEqual(len(entries), 1)
                self.assertLessEqual(len(entries), 5)
                for number, multiplier in entries:
                    number, multiplier = int(number), int(multiplier)
                    self.assertIn(number, range(37))
                    self.assertNotIn(number, multipliers)
                    self.assertIn(multiplier, (50, 100, 200, 500))
                    multipliers[number] = multiplier
            else:
                self.assertEqual(" off", lucky_match[1])
            result = re.search(r"^  result: number=(\d+) pocket=(\d+)$", body, re.MULTILINE)
            fault = re.search(r"^  VOID: (\w+) \| all accepted stakes refunded$", body, re.MULTILINE)
            self.assertNotEqual(result is None, fault is None)
            if fault:
                voided += 1
                self.assertEqual(0, index)
                self.assertEqual(injected_fault, fault[1])
                self.assertEqual(["Idle", "BetsOpen", "BetsClosing", "BetsClosed", "Spinning", "Fault", "Idle"], states)
            else:
                settled += 1
                number, pocket = int(result[1]), int(result[2])
                self.assertIn(pocket, range(37))
                self.assertEqual(WHEEL_ORDER[pocket], number)
                self.assertEqual(["Idle", "BetsOpen", "BetsClosing", "BetsClosed", "Spinning", "Result", "Payout", "Idle"], states)

            seats = list(SEAT_LINE.finditer(body))
            self.assertEqual(list(range(8)), [int(seat[1]) for seat in seats])
            stake_total = 0
            return_total = 0
            accepted = 0
            for seat_line in seats:
                seat = int(seat_line[1])
                pattern = seat_line[3]
                stake, gross, refund, net, after = map(int, seat_line.groups()[3:])
                stake_total += stake
                return_total += gross
                if pattern.startswith("skipped"):
                    skipped += 1
                    self.assertEqual("skipped (no affordable stake)", pattern)
                    self.assertEqual(0, credits[seat])
                    self.assertEqual((0, 0, 0, 0, 0), (stake, gross, refund, net, after))
                else:
                    accepted += 1
                    bet = re.fullmatch(r"(\w+)\[([\d,]+)\]", pattern)
                    self.assertIsNotNone(bet)
                    kind = bet[1]
                    covered = list(map(int, bet[2].split(",")))
                    self.assertEqual(sorted(set(covered)), covered)
                    self.assertTrue(all(value in range(37) for value in covered))
                    self.assertIn(kind, GROSS_ODDS)
                    desired = 10 if 2 <= seat <= 5 else 5
                    self.assertEqual(min(desired, credits[seat]), stake)
                    if fault:
                        self.assertEqual(0, gross)
                        self.assertEqual(stake, refund)
                    else:
                        self.assertEqual(0, refund)
                        multiplier = GROSS_ODDS[kind]
                        if lucky and kind == "Straight":
                            multiplier = multipliers.get(number, 30)
                        self.assertEqual(stake * multiplier if number in covered else 0, gross)
                        played[seat] += 1
                        won[seat] += gross > stake
                    self.assertEqual(gross + refund - stake, net)
                    self.assertEqual(credits[seat] + net, after)
                credits[seat] = after

            if not fault:
                house += stake_total - return_total
            reported_house = re.search(r"^  house=(-?\d+) \| conservation=OK$", body, re.MULTILINE)
            self.assertIsNotNone(reported_house)
            self.assertEqual(house, int(reported_house[1]))
            self.assertEqual(8000, sum(credits) + house)
            expected_events = 2 * accepted + len(states) - 1 + int(lucky)
            if result:
                expected_events += 1  # ResultRecorded.
            elif fault[1] == "PowerLoss":
                expected_events += 1  # RecoveryApplied shares the refund commit.
            self.assertEqual(expected_events, last_sequence - sequence)
            sequence = last_sequence

        final = text[text.index("FINAL METERS"):]
        summary = re.search(r"^FINAL METERS \| settled=(\d+) voided=(\d+)$", final, re.MULTILINE)
        self.assertIsNotNone(summary)
        self.assertEqual((settled, voided), tuple(map(int, summary.groups())))
        meters = re.findall(r"^(\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$", final, re.MULTILINE)
        self.assertEqual(8, len(meters))
        for seat, fields in enumerate(meters):
            self.assertEqual((seat, credits[seat], 1000, 0, played[seat], won[seat]), tuple(map(int, fields)))
        final_total = re.search(r"^seat_total=(\d+) house=(-?\d+) combined=(\d+) initial=(\d+)$", final, re.MULTILINE)
        self.assertIsNotNone(final_total)
        self.assertEqual((sum(credits), house, 8000, 8000), tuple(map(int, final_total.groups())))
        journal = re.search(r"^journal_events=(\d+) \| final_state=Idle \| conservation=OK$", final, re.MULTILINE)
        self.assertIsNotNone(journal)
        self.assertEqual(sequence, int(journal[1]))
        return skipped

    def test_help_does_not_run_or_initialize_a_session(self):
        for arguments in (("--help",), ("-h",), ("--rounds=10000", "--help")):
            with self.subTest(arguments=arguments):
                text = self.successful(*arguments)
                self.assertIn("Usage: roulette_sim", text)
                self.assertNotIn("ROUND ", text)
                self.assertNotIn("FINAL METERS", text)
                self.assertNotIn("wheel_seed=", text)

    def test_invalid_and_duplicate_options_fail_before_session_output(self):
        invalid = [
            ["--rounds", "0"], ["--rounds=10001"], ["--rounds", "-1"],
            ["--rounds=1.5"], ["--rounds=4junk"], ["--rounds", "+1"],
            ["--rounds", ""], ["--rounds"], ["--seed"], ["--inject-fault"],
            ["--seed", "4294967296"], ["--seed=-1"], ["--seed", " 1"],
            ["--seed=18446744073709551616"], ["--seed", "+1"],
            ["--seed", "1", "--seed", "2"], ["--seed=1", "--seed=2"],
            ["--seed=1", "--seed", "2"], ["--rounds", "1", "--rounds=2"],
            ["--rounds=1", "--rounds=2"],
            ["--inject-fault=none", "--inject-fault", "no-spin"],
            ["--inject-fault=none", "--inject-fault=no-spin"],
            ["--lucky-mode", "--lucky-mode"], ["--help", "-h"],
            ["--lucky-mode=false"], ["--unknown", "1"], ["unexpected"],
            ["--inject-fault=bad-sensor"], ["--inject-fault="], ["--help=1"],
        ]
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments)
                self.assertEqual(2, result.returncode)
                self.assertEqual("", result.stdout)
                self.assertIn("Usage:", result.stderr)

    def test_numeric_boundaries_and_both_option_forms_agree(self):
        for seed in (0, 4294967295):
            with self.subTest(seed=seed):
                separate = self.successful("--seed", seed, "--rounds", "1", "--inject-fault", "none")
                equals = self.successful(f"--seed={seed}", "--rounds=1", "--inject-fault=none")
                self.assertEqual(separate, equals)
                self.verify_transcript(separate, 1, False)

    def test_same_seed_repeats_complete_stdout(self):
        for mode in ((), ("--lucky-mode",)):
            arguments = ("--seed=1234567", "--rounds=20", *mode)
            self.assertEqual(self.successful(*arguments), self.successful(*arguments))

    def test_different_seed_changes_wheel_results(self):
        first = self.successful("--seed=1", "--rounds=20")
        second = self.successful("--seed=2", "--rounds=20")
        self.assertNotEqual(self.result_sequence(first), self.result_sequence(second))

    def test_lucky_toggle_keeps_the_same_wheel_sequence(self):
        standard = self.successful("--seed=20261111", "--rounds=30")
        lucky = self.successful("--seed=20261111", "--rounds=30", "--lucky-mode")
        self.assertEqual(self.result_sequence(standard), self.result_sequence(lucky))
        self.verify_transcript(standard, 30, False)
        self.verify_transcript(lucky, 30, True)

    def test_all_faults_refund_first_round_and_keep_later_wheel_results(self):
        faults = {
            "no-spin": "NoSpin", "ball-timeout": "BallTimeout",
            "invalid-sensor": "InvalidSensor", "start-failure": "WheelStartFailed",
            "power-loss": "PowerLoss",
        }
        for mode in ((), ("--lucky-mode",)):
            baseline = self.successful("--seed=42", "--rounds=3", *mode)
            expected_results = self.result_sequence(baseline)[1:]
            for option, expected_fault in faults.items():
                with self.subTest(mode=mode, fault=option):
                    text = self.successful("--seed=42", "--rounds=3", *mode, f"--inject-fault={option}")
                    self.verify_transcript(text, 3, bool(mode), expected_fault)
                    self.assertEqual(expected_results, self.result_sequence(text))
                    self.assertEqual(1, text.count("  VOID:"))

    def test_printed_payouts_and_final_meters_match_independent_accounting(self):
        for mode in ((), ("--lucky-mode",)):
            with self.subTest(mode=mode):
                text = self.successful("--seed=987654321", "--rounds=50", *mode)
                self.verify_transcript(text, 50, bool(mode))

    def test_long_run_skips_broke_seats_without_automatic_credit_loads(self):
        # This seed first reaches a broke-seat skip at round 871. Exercise the
        # maximum accepted run length as well as the no-automatic-funding rule.
        text = self.successful("--seed=20261111", "--rounds=10000", "--lucky-mode")
        skipped = self.verify_transcript(text, 10000, True)
        self.assertGreater(skipped, 0, "This fixture must exercise at least one broke-seat skip")


if __name__ == "__main__":
    unittest.main()
