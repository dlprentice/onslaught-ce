#!/usr/bin/env python3
"""Compile the authored native-dialogue reservation collector and adverse controls."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    base = root / "local-data/ce-hud-reservation-tests"
    base.mkdir(parents=True, exist_ok=True)
    out = args.out or Path(tempfile.mkdtemp(prefix="run-", dir=base))
    out.mkdir(parents=True, exist_ok=True)
    source = root / "port/game/ce_hud_reservation.cpp"
    header = root / "port/game/ce_hud_reservation.h"
    driver = root / "port/tools/tests/ce_hud_reservation_driver.cpp"
    variants = {"actual": source.read_text()}
    mutations = {
        "stale_frame": ("frame != gFrame ||", "(frame != gFrame && false) ||"),
        "stale_owner": ("owner != gOwner ||", "false ||"),
        "invisible_sprite": ("(color >> 24) == 0", "((color >> 24) == 0 && false)"),
    }
    for name, (before, after) in mutations.items():
        if variants["actual"].count(before) != 1:
            raise ValueError("reservation mutation anchor changed: " + name)
        variants[name] = variants["actual"].replace(before, after, 1)
    results = {}
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    for name, text in variants.items():
        candidate = out / (name + ".cpp")
        candidate.write_text(text)
        executable = out / name
        command = ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I", str(header.parent), str(candidate), str(driver), "-o", str(executable)]
        with (out / (name + ".compile.txt")).open("wb") as log:
            compiled = subprocess.run(command, stdout=log, stderr=log)
        if compiled.returncode:
            results[name] = {"compiled": False}
            continue
        run = subprocess.run([str(executable)], capture_output=True, env=env)
        (out / (name + ".run.txt")).write_bytes(run.stdout + run.stderr)
        try:
            report = json.loads(run.stdout)
        except (ValueError, UnicodeDecodeError):
            report = {}
        results[name] = {"compiled": True, "exit": run.returncode, "report": report}
    passed = (results.get("actual", {}).get("exit") == 0 and
              results["actual"].get("report", {}).get("skipped") == 0 and
              all(results.get(name, {}).get("compiled") and
                  results[name].get("exit") == 1 and
                  results[name].get("report", {}).get("failures", 0) > 0
                  for name in mutations))
    receipt = {"passed": passed, "results": results,
               "inputs": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (source, header, driver)}}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "receipt": str(out / "result.json")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
