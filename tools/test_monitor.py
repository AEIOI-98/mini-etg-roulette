#!/usr/bin/env python3
"""Verify the telemetry receiver with known packets and interrupted byte streams."""

import binascii
import io
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest

import monitor


# Independent CRC and escaping vectors also used in the native suite.
HELLO = bytes.fromhex("7e 05 34 12 48 65 6c 6c 6f d9 bb")
ESCAPED = bytes.fromhex("7e 02 7d 5e 7d 5d 7d 5e 7d 5d 95 ab")


def packet(sequence, payload=b""):
    body = bytes([len(payload)]) + sequence.to_bytes(2, "little") + payload
    body += binascii.crc_hqx(body, 0xFFFF).to_bytes(2, "little")
    return b"~" + body.replace(b"}", b"}\x5d").replace(b"~", b"}\x5e")


def run_monitor(binary):
    output = io.StringIO()
    errors = io.StringIO()
    result = monitor.monitor(io.BytesIO(binary), False, output, errors)
    return result, output.getvalue(), errors.getvalue()


class TelemetryTests(unittest.TestCase):
    def test_known_stuffed_packets_and_noise(self):
        rc, output, errors = run_monitor(b"ESP-ROM\r\n" + HELLO + ESCAPED)
        self.assertEqual(rc, 0)
        self.assertEqual(output, "04660 Hello\n32126 ~}\n")
        self.assertIn("expected 4661, got 32126", errors)

    def test_fragmented_port_reads_and_timeout(self):
        class Port:
            def __init__(self):
                sequence = [b"ESP\n", HELLO[:2], b"", HELLO[2:5], HELLO[5:]]
                self.reads = iter(sequence)

            def read(self, size):
                try:
                    return next(self.reads)
                except StopIteration:
                    raise KeyboardInterrupt

        output, errors = io.StringIO(), io.StringIO()
        with self.assertRaises(KeyboardInterrupt):
            monitor.monitor(Port(), True, output, errors)
        self.assertEqual(output.getvalue(), "04660 Hello\n")
        self.assertEqual(errors.getvalue(), "")

    def test_faults_resume_at_the_next_good_marker(self):
        corrupt = bytearray(HELLO)
        corrupt[5] ^= 4
        for damaged, diagnostic in [(corrupt, "CRC mismatch"), (b"~\xff", "invalid length"),
                                    (b"~}x", "invalid escape"), (b"~\x10}", "truncated frame")]:
            with self.subTest(diagnostic=diagnostic):
                rc, output, errors = run_monitor(damaged + HELLO)
                self.assertEqual(rc, 1)
                self.assertEqual(output, "04660 Hello\n")
                self.assertEqual(errors.strip(), diagnostic)
        for cut in range(1, len(HELLO)):
            self.assertEqual(run_monitor(HELLO[:cut]), (1, "", "truncated frame\n"))

    def test_max_payload_wrap_and_terminal_control_bytes(self):
        data = packet(65535, b"}~" * 48) + packet(0, b"\x1b\n\xff") + packet(1)
        rc, output, errors = run_monitor(data)
        self.assertEqual(rc, 0)
        self.assertEqual(errors, "")
        self.assertEqual(output, "65535 " + "}~" * 48 + "\n00000 \\x1b\\x0a\\xff\n00001 \n")

    def test_cli_file_and_stdin_inputs(self):
        command = [sys.executable, str(Path(monitor.__file__))]
        with TemporaryDirectory() as directory:
            file = Path(directory) / "uart.bin"
            file.write_bytes(HELLO)
            self.assertEqual(subprocess.check_output(command + ["--file", str(file)]), b"04660 Hello\n")
        self.assertEqual(subprocess.check_output(command, input=HELLO), b"04660 Hello\n")


if __name__ == "__main__":
    unittest.main()
