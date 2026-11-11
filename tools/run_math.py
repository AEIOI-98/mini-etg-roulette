#!/usr/bin/env python3
"""Build and run the C++ core's seeded Phase 2 math experiment (stdlib only)."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def bounded_integer(minimum, maximum):
    def parse(text):
        if not text.isascii() or not text.isdecimal():
            raise argparse.ArgumentTypeError("use an unsigned decimal integer")
        value = int(text)
        if not minimum <= value <= maximum:
            raise argparse.ArgumentTypeError(f"must be between {minimum} and {maximum}")
        return value
    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spins", type=bounded_integer(2, 1_000_000_000), default=10_000_000)
    parser.add_argument("--seed", type=bounded_integer(0, 2**32 - 1), default=20261111)
    parser.add_argument("--output", type=Path, default=ROOT / "tools" / "results")
    parser.add_argument("--build-only", action="store_true", help="compile without running")
    args = parser.parse_args()
    build = ROOT / "tools" / ".build"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "roulette_math"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        parser.error("CXX must name a C++ compiler")
    command = compiler + [
        "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fno-exceptions", "-fno-rtti", "-I", str(ROOT / "core" / "include"),
        str(ROOT / "tools" / "math.cpp"),
        *map(str, sorted((ROOT / "core" / "src").glob("*.cpp"))),
        "-o", str(binary),
    ]
    try:
        subprocess.run(command, check=True)
        if not args.build_only:
            subprocess.run([
                str(binary), "--spins", str(args.spins), "--seed", str(args.seed),
                "--output", str(args.output.resolve()),
            ], check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Math experiment failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
