#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned restart records against authored, retained local fixtures only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/ce_netcode.cpp"
DRIVER = ROOT / "port/tools/tests/ce_handoff_recovery_driver.cpp"


def replace_function(text: str, name: str, body: str) -> str:
    match = re.search(r"static bool " + re.escape(name) + r"\([^{}]*\)\s*\{", text)
    if not match:
        raise RuntimeError("missing owned-fixture mutation anchor")
    start = match.end()
    depth, end = 1, start
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[:start] + "\n    " + body + "\n" + text[end - 1:]


def replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise RuntimeError("ambiguous owned-fixture mutation anchor")
    return text.replace(before, after)


def command(args: list[str], log: Path) -> int:
    with log.open("w") as output:
        return subprocess.run(args, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                              timeout=45).returncode


def trial(root: Path, label: str, source: str, m32: bool,
          expected_failure: str | None) -> dict:
    directory = root / label
    directory.mkdir(mode=0o700)
    cases = directory / "cases"
    cases.mkdir(mode=0o700)
    copied = directory / "codec.cpp"
    relocated = re.sub(r'^([ \t]*#include[ \t]+)"([^"\n]+)"',
                       lambda match: match[1] + '"' +
                       str((SOURCE.parent / match[2]).resolve()) + '"',
                       source, flags=re.MULTILINE)
    copied.write_text(relocated)
    flags = ["-m32"] if m32 else []
    sha = directory / "sha256.o"
    compiler = os.environ.get("CXX", "c++")
    c_compiler = os.environ.get("CC", "cc")
    c_result = command([c_compiler, *flags, "-std=c99", "-c",
                        str(ROOT / "port/updater/sha256.c"), "-o", str(sha)],
                       directory / "sha-build.private.log")
    binary = directory / "fixture"
    build_result = c_result
    if c_result == 0:
        build_result = command([compiler, *flags, "-std=c++17", "-O0", "-g0",
                                str(DRIVER), str(copied), str(sha),
                                "-Wl,--wrap=ftruncate", "-Wl,--wrap=write", "-Wl,--wrap=close",
                                "-o", str(binary)], directory / "build.private.log")
    result = {"label": label, "compiled": build_result == 0, "m32": m32,
              "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
              "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest(),
              "expected_negative": expected_failure is not None}
    if build_result:
        result.update(passed=False, build_exit_code=build_result)
        return result
    exit_code = command([str(binary), str(cases)], directory / "run.private.log")
    log = (directory / "run.private.log").read_text()
    counts = re.search(r"owned handoff: checks=(\d+) failures=(\d+)", log)
    result.update(exit_code=exit_code,
                  checks=int(counts[1]) if counts else 0,
                  failures=int(counts[2]) if counts else 0,
                  fixture_files=sum(p.is_file() for p in cases.iterdir()))
    if expected_failure is None:
        result["passed"] = exit_code == 0 and counts is not None and int(counts[2]) == 0
    else:
        result["intended_assertion_observed"] = expected_failure in log
        result["passed"] = exit_code != 0 and counts is not None and expected_failure in log
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m32", action="store_true")
    args = parser.parse_args()
    os.umask(0o077)
    evidence_root = ROOT / "local-data/port/ce-handoff-recovery-tests"
    evidence_root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=evidence_root))
    source = SOURCE.read_text()
    mutations = [
        ("identity-binding-removed", replace_function(source, "OwnerIdentityEqual", "return true;"),
         "unknown record never reaches cleanup"),
        ("content-binding-removed", replace_function(source, "OwnerDigestEqual", "return true;"),
         "unknown record never reaches cleanup"),
        ("canonical-owner-removed", replace_once(source,
         "bool ok = n == size && memcmp(canonical, text, size) == 0;", "bool ok = true;"),
         "noncanonical owner is refused and cleared"),
        ("cleanup-admission-gate-removed", replace_once(source,
         "bool ok = parsed && cleared && closed;", "bool ok = parsed && closed;"),
         "cleanup failure cannot report admission"),
    ]
    results = [trial(output, "actual", source, args.m32, None)]
    if results[0]["passed"]:
        for label, mutant, expected in mutations:
            results.append(trial(output, label, mutant, args.m32, expected))
    passed = len(results) == 5 and all(item["passed"] for item in results)
    receipt = {"passed": passed, "results": results,
               "fixture_policy": "All fixture entries and evidence are retained. No unlink or rename.",
               "limits": ["Authored local filesystem cases; no game, desktop, network or real user data.",
                          "Negative identity controls block physical cleanup while recording any attempted cleanup.",
                          "No native Windows or concurrent pathname-race acceptance."]}
    (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": passed, "compiled_cases": sum(r["compiled"] for r in results),
                      "receipt": str(output / "result.json")}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
