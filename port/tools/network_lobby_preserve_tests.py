#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise host replacement with only a local fake transport and interfaces."""

from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/ce_netlobby.cpp"
DRIVER = ROOT / "port/tools/tests/network_lobby_preserve_driver.cpp"


def extract(source, name):
    match = re.search(r"(?m)^[^\n;{}]*\b" + name + r"\s*\([^;{}]*\)\s*\{", source)
    if not match:
        raise ValueError("host function missing")
    start = match.start()
    end = match.end()
    depth = 1
    while end < len(source) and depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("unterminated host function")
    return source[start:end]


def main():
    compiler = shutil.which("c++")
    if not compiler:
        print("lobby preservation: FAIL; C++ compiler unavailable")
        return 1
    source = SOURCE.read_text()
    names = ("CreateProblem", "LobbyHost", "CE_LobbyHost", "CE_LobbyHostAt")
    functions = "\n\n".join(extract(source, name) for name in names)
    fixture = DRIVER.read_text()
    marker = "/* INSERT_HOST_FUNCTIONS */"
    if fixture.count(marker) != 1:
        print("lobby preservation: FAIL; fixture marker changed")
        return 1
    os.umask(0o077)
    parent = ROOT / "local-data/port/network-lobby-preserve-tests"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    results = []
    def run_variant(name, body):
        program = output / (name + ".cpp")
        binary = output / name
        program.write_text(fixture.replace(marker, body))
        built = subprocess.run([compiler, "-std=c++11", "-O0", str(program), "-o", str(binary)],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-build.log")).write_bytes(built.stdout)
        if built.returncode:
            results.append({"name": name, "build_exit": built.returncode, "run_exit": None})
            return None
        result = subprocess.run([str(binary)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=30)
        (output / (name + "-run.log")).write_bytes(result.stdout)
        results.append({"name": name, "build_exit": 0, "run_exit": result.returncode})
        return result.returncode

    def finish(passed):
        receipt = {"passed": passed, "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                   "driver_sha256": hashlib.sha256(DRIVER.read_bytes()).hexdigest(), "results": results,
                   "scope": "Extracted host callers and error lookup, authored interfaces and observable fake transport; no sockets or game."}
        (output / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        return 0 if passed else 1

    result = run_variant("control", functions)
    if result != 0:
        print("lobby preservation: FAIL;", "control compile failed" if result is None else f"synthetic case {result}",
              "; private evidence:", output)
        return finish(False)
    anchor = "uint32_t selected_ip = 0;"
    if functions.count(anchor) != 1:
        print("lobby preservation: FAIL; mutation anchor changed")
        return finish(False)
    mutant = functions.replace(anchor, "Forget();\n    " + anchor, 1)
    if run_variant("mutant", mutant) != 2:
        print("lobby preservation: FAIL; early-reset mutant not rejected")
        return finish(False)
    for label, before, after, expected in (
        ("backend-error-lost", "backend_problem && backend_problem[0]", "false", 13),
        ("failed-code-retained", "CE_CodeWipe(L.st.code, sizeof L.st.code);", "(void)0;", 14),
    ):
        if functions.count(before) != 1:
            print("lobby preservation: FAIL; mutation anchor changed:", label)
            return finish(False)
        if run_variant(label, functions.replace(before, after, 1)) != expected:
            print("lobby preservation: FAIL; intended negative not observed:", label)
            return finish(False)
    print("lobby preservation: PASS; early-reset and listener-failure mutants rejected; private evidence:", output)
    return finish(True)


if __name__ == "__main__":
    sys.exit(main())
