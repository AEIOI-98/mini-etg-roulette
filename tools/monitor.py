#!/usr/bin/env python3
"""Decode the ESP32 controller's CRC-framed UART telemetry from a port, file or stdin."""

import argparse
import binascii
from contextlib import nullcontext
from dataclasses import dataclass
from pathlib import Path
import sys


START = 0x7E
ESCAPE = 0x7D
MAX_PAYLOAD = 96


@dataclass(frozen=True)
class Frame:
    sequence: int
    payload: bytes


class Decoder:
    """A partial body survives short reads. An unescaped start always resynchronizes."""

    def __init__(self):
        self.body = None
        self.escaped = False

    def finish(self):
        incomplete = self.body is not None
        self.body = None
        self.escaped = False
        return "truncated frame" if incomplete else None

    def feed(self, byte):
        if byte == START:
            error = self.finish()
            self.body = bytearray()
            return error
        if self.body is None:
            return None
        if self.escaped:
            if byte not in (START ^ 0x20, ESCAPE ^ 0x20):
                self.finish()
                return "invalid escape"
            byte ^= 0x20
            self.escaped = False
        elif byte == ESCAPE:
            self.escaped = True
            return None

        body = self.body
        if not body and byte > MAX_PAYLOAD:
            self.finish()
            return "invalid length"
        body.append(byte)
        if len(body) < 3 + body[0] + 2:
            return None

        self.finish()
        expected = int.from_bytes(body[-2:], "little")
        if binascii.crc_hqx(body[:-2], 0xFFFF) != expected:
            return "CRC mismatch"
        return Frame(int.from_bytes(body[1:3], "little"), bytes(body[3:-2]))


def display_payload(payload):
    # Terminal control bytes, including escape, are displayed as hex.
    return "".join(chr(byte) if 32 <= byte <= 126 else f"\\x{byte:02x}" for byte in payload)


def monitor(source, live, output, errors):
    decoder = Decoder()
    next_sequence = None
    damaged = False
    read = getattr(source, "read1", source.read)
    while True:
        data = read(256)
        if not data:
            if live:
                continue  # A serial timeout can occur in the middle of a frame.
            break
        for byte in data:
            result = decoder.feed(byte)
            if result is None:
                continue
            if isinstance(result, str):
                print(result, file=errors, flush=True)
                damaged = True
                continue
            if next_sequence is not None and result.sequence != next_sequence:
                print(f"sequence discontinuity: expected {next_sequence}, got {result.sequence}",
                      file=errors, flush=True)
            next_sequence = (result.sequence + 1) & 0xFFFF
            print(f"{result.sequence:05d} {display_payload(result.payload)}", file=output, flush=True)
    trailing_error = decoder.finish()
    if trailing_error:
        print(trailing_error, file=errors)
    return int(damaged or trailing_error is not None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--port", help="serial device or pySerial URL")
    selection.add_argument("--file", type=Path, help="raw binary UART capture (default: stdin)")
    parser.add_argument("--baud", type=int, default=115200, help="port baud rate (default: 115200)")
    args = parser.parse_args()
    if args.baud <= 0:
        parser.error("--baud must be positive")
    try:
        if args.port:
            try:
                import serial
            except ImportError:
                parser.exit(2, "Reading a port requires pySerial: python3 -m pip install pyserial\n")
            connection = serial.serial_for_url(args.port, baudrate=args.baud, timeout=0.1,
                                               do_not_open=True)
            connection.dtr = False
            connection.rts = False
            # pySerial's context manager opens a closed port and closes it on exit.
        elif args.file:
            connection = args.file.open("rb")
        else:
            connection = nullcontext(sys.stdin.buffer)
        with connection as source:
            return monitor(source, bool(args.port), sys.stdout, sys.stderr)
    except KeyboardInterrupt:
        return 130
    except (OSError, ValueError) as error:
        print(f"Serial monitor: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
