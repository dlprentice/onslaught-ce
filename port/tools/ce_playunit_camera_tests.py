#!/usr/bin/env python3
"""Compile the unit camera law and optional behavioral counterexamples locally."""
# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function_region(text: str, name: str) -> tuple[int, int]:
    clean = re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: " " * len(m.group()), text, flags=re.S)
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", clean)
    if not match:
        raise ValueError(f"function absent: {name}")
    start = clean.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (clean[end] == "{") - (clean[end] == "}")
        end += 1
    return start, end


def change_body(text: str, name: str, change) -> str:
    start, end = function_region(text, name)
    return text[:start] + change(text[start:end]) + text[end:]


def before_last_return(text: str, name: str, statement: str) -> str:
    def change(body):
        clean = re.sub(r"//[^\n]*", lambda m: " " * len(m.group()), body)
        matches = list(re.finditer(r"\breturn\b", clean))
        if not matches:
            raise ValueError(f"return absent: {name}")
        offset = matches[-1].start()
        return body[:offset] + statement + "\n    " + body[offset:]
    return change_body(text, name, change)


def counterexamples(source: str) -> dict[str, str]:
    def absolute_eye(body):
        result, count = re.subn(r"eye\[2\]\s*=\s*[^;]+;", "eye[2] = -1.55f;", body)
        if count != 1:
            raise ValueError("eye assignment changed")
        return result
    return {
        "absolute-eye": change_body(source, "CePlayUnitCameraFootEye", absolute_eye),
        "fixed-span": before_last_return(source, "CePlayUnitCameraMeasure", "*span = 1.0f;"),
        "snap-view": change_body(source, "CePlayUnitCameraAdvance",
                                 lambda body: "{ CePlayUnitCameraReset(s, identity, s->view); }"),
        "fixed-frame-time": change_body(source, "CePlayUnitCameraAdvance",
                                        lambda body: body[:1] + "\n    dt = 1.0f / 60.0f;" + body[1:]),
        "steer-look-to-zero": change_body(source, "CePlayUnitCameraPoseFor",
                                          lambda body: body[:1] + "\n    yaw = 0.0f;" + body[1:]),
        "never-hide-body": before_last_return(source, "CePlayUnitCameraPoseFor", "out->hideBody = false;"),
        "unavailable-first-person": change_body(source, "CePlayUnitCameraTop", lambda body: "{ return 2; }"),
    }


def run_case(out: Path, name: str, header: str, cxx: str, sanitize: bool = False) -> dict:
    case = out / name
    case.mkdir()
    (case / "ce_playunit_camera.h").write_text(header)
    shutil.copyfile(ROOT / "port/tools/tests/ce_playunit_camera_driver.cpp", case / "driver.cpp")
    shutil.copyfile(ROOT / "port/game/ce_playunit_look.h", case / "ce_playunit_look.h")
    executable = case / "driver"
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else ["-O2"]
    command = [cxx, "-std=c++17", *flags, "-Wall", "-Wextra", "-pedantic", str(case / "driver.cpp"), "-lm", "-o", str(executable)]
    compiled = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (case / "compile.log").write_text(compiled.stdout)
    result = {"case": name, "compile_exit": compiled.returncode,
              "header_sha256": hashlib.sha256(header.encode()).hexdigest(), "sanitized": sanitize}
    if compiled.returncode == 0:
        ran = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (case / "run.log").write_text(ran.stdout)
        summary = re.search(r"(\d+) checks, (\d+) failed", ran.stdout)
        result.update(run_exit=ran.returncode,
                      checks=int(summary.group(1)) if summary else None,
                      failed=int(summary.group(2)) if summary else None,
                      sanitizer_error=bool(re.search(r"AddressSanitizer|runtime error:", ran.stdout)))
    positive = name in {"normal", "sanitized"}
    result["pass"] = (result.get("checks") == 242 and
                      result.get("run_exit") == (0 if positive else 1) and
                      (result.get("failed") == 0 if positive else bool(result.get("failed"))) and
                      not result.get("sanitizer_error", False))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--causal-controls", action="store_true", help="compile and reject seven wrong camera laws")
    parser.add_argument("--sanitize", action="store_true", help="also run the positive with address/undefined sanitizers")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--out", type=Path, default=ROOT / "local-data/ce-playunit-camera-tests")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="run-", dir=args.out))
    header = (ROOT / "port/game/ce_playunit_camera.h").read_text()
    cases = {"normal": header}
    if args.causal_controls:
        cases.update(counterexamples(header))
    results = [run_case(out, name, source, args.cxx) for name, source in cases.items()]
    if args.sanitize:
        results.append(run_case(out, "sanitized", header, args.cxx, True))
    receipt = {"cases": results, "passed": sum(r["pass"] for r in results), "skipped": 0,
               "driver_sha256": hashlib.sha256((ROOT / "port/tools/tests/ce_playunit_camera_driver.cpp").read_bytes()).hexdigest()}
    (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "total": len(results), "skipped": 0, "output": str(out)}))
    return 0 if all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
