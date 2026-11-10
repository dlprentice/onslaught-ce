#!/usr/bin/env python3
# SPDX-FileCopyrightText: Onslaught CE contributors
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the shared projection helper against authored camera geometry."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    folder = root / "local-data/view-projection-tests"
    folder.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=folder))
    compiler = shutil.which("c++")
    if not compiler:
        raise SystemExit("C++ compiler is required; no checks ran")
    source = (root / "port/game/ce_view_projection.h").read_text()
    cases = {"actual": source,
             "ignore_view": source.replace("double(view[row * 4 + k])", "double(row == k)"),
             "invert_screen_y": source.replace("(1 - ndc.y)", "(1 + ndc.y)"),
             "allow_clipped": source.replace("ndc.z < 0 || ndc.z > 1", "false"),
             "reverse_ray": source.replace("*nearPoint = nearValue; *farPoint = farValue", "*nearPoint = farValue; *farPoint = nearValue")}
    results = {}
    for name, content in cases.items():
        if name != "actual" and content == source:
            raise SystemExit("causal control anchor missing")
        case = out / name
        case.mkdir()
        (case / "ce_view_projection.h").write_text(content)
        exe = case / "checks"
        build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(case),
                                str(root / "port/tools/ce_view_projection_tests.cpp"), "-o", str(exe)],
                               capture_output=True, timeout=40)
        (case / "compile.private.log").write_bytes(build.stdout + build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, timeout=15) if build.returncode == 0 else None
        if run:
            (case / "run.private.log").write_bytes(run.stdout + run.stderr)
        results[name] = {"compiled": build.returncode == 0, "exit": run.returncode if run else None,
                         "expected": 0 if name == "actual" else "nonzero"}
    passed = all(v["compiled"] and (v["exit"] == 0 if k == "actual" else v["exit"] != 0) for k, v in results.items())
    (out / "RESULT.private.json").write_text(json.dumps({"passed": passed, "cases": results}, indent=2) + "\n")
    print(json.dumps({"passed": passed, "evidence": str(out), "scope": "authored geometry only; rendered camera adapter not exercised"}))
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
