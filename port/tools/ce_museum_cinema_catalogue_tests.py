#!/usr/bin/env python3
"""Authored installed-film enumeration and exact decoder-index controls."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "local-data" / "museum-cinema-catalogue-tests"
OUT.mkdir(parents=True, exist_ok=True)
for mode, flags in (("warnings", ["-O2"]),
                    ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
    binary = OUT / mode
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I", str(ROOT / "port/game"),
                    str(ROOT / "port/tools/tests/ce_museum_cinema_catalogue_driver.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
