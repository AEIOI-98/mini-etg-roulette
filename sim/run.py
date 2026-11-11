#!/usr/bin/env python3
"""Build the native simulator and pass its CLI arguments through unchanged."""

import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    arguments = sys.argv[1:]
    build_only = arguments == ["--build-only"]
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        print("CXX must name a C++ compiler", file=sys.stderr)
        return 1
    build = ROOT / "sim" / ".build"
    build.mkdir(parents=True, exist_ok=True)
    binary = build / "roulette_sim"
    command = compiler + [
        "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fno-exceptions", "-fno-rtti",
        "-I", str(ROOT / "core" / "include"),
        "-I", str(ROOT / "sim" / "shared" / "include"),
        str(ROOT / "sim" / "src" / "main.cpp"),
        *map(str, sorted((ROOT / "core" / "src").glob("*.cpp"))),
        *map(str, sorted((ROOT / "sim" / "shared" / "src").glob("*.cpp"))),
        "-o", str(binary),
    ]
    try:
        subprocess.run(command, check=True)
        if build_only:
            return 0
        return subprocess.run([str(binary), *arguments], check=False).returncode
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Simulator failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
