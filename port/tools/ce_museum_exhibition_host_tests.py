#!/usr/bin/env python3
"""Compile and exercise the authored exhibition-to-native coordinate contract."""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "port/tools/tests/ce_museum_exhibition_host_driver.cpp"
OUT = ROOT / "local-data/ce-museum-exhibition-host-tests"
SUMMARY = re.compile(r"^ce_museum_exhibition_host_driver: (\d+) checks, (\d+) failed$", re.M)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="contract-", dir=OUT))
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    sources = [DRIVER] + [ROOT / "port/game" / name for name in (
        "ce_museum_exhibition.h", "ce_museum_exhibition_host.h", "ce_museum_world.h",
        "ce_museum_cinema.h", "ce_museum_hall.h")]
    pins = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    modes = []
    for name, flags in (
            ("normal", ["-O2"]),
            ("sanitize", ["-O1", "-g", "-fsanitize=address,undefined",
                          "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"])):
        folder = run / name
        folder.mkdir()
        binary = folder / "host-contract"
        command = compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        command += flags + [str(DRIVER), "-o", str(binary)]
        with (folder / "build.log").open("w") as log:
            build = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        result = {"mode": name, "build_exit": build.returncode, "run_exit": None,
                  "checks": 0, "failed": 0, "skipped": False, "passed": False}
        if build.returncode == 0:
            environment = os.environ.copy()
            environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            with (folder / "run.log").open("w") as log:
                actual = subprocess.run([str(binary)], cwd=ROOT, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=60)
            result["run_exit"] = actual.returncode
            summary = SUMMARY.search((folder / "run.log").read_text())
            if summary:
                result["checks"], result["failed"] = map(int, summary.groups())
            result["passed"] = bool(summary and actual.returncode == 0 and
                                    result["checks"] >= 79 and result["failed"] == 0)
        modes.append(result)
        print("%s: %s, %d checks, %d failed, no skips" %
              (name, "PASS" if result["passed"] else "FAIL", result["checks"], result["failed"]))
    unchanged = all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == sha
                    for name, sha in pins.items())
    passed = unchanged and all(mode["passed"] for mode in modes)
    receipt = {"passed": passed, "source_pins_unchanged": unchanged, "sources": pins,
               "modes": modes, "run": str(run.relative_to(ROOT))}
    (run / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
