#!/usr/bin/env python3
"""Original console geometry, actual-controller composition and measured text tests."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "local-data" / "museum-cinema-console-tests"
OUT.mkdir(parents=True, exist_ok=True)
for mode, flags in (("warnings", ["-O2"]),
                    ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
    binary = OUT / mode
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I", str(ROOT / "port/game"),
                    str(ROOT / "port/game/ce_museum_cinema_console.cpp"),
                    str(ROOT / "port/tools/tests/ce_museum_cinema_console_driver.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
