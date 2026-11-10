#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual text owner with invented, offline lifecycle controls."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_AS, (2 * 1024**3, 2 * 1024**3))
    resource.setrlimit(resource.RLIMIT_CPU, (30, 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 * 1024**2, 16 * 1024**2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--word-size", choices=("32", "64", "both"), default="both")
    parser.add_argument("--no-controls", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    evidence = repo / "local-data" / "port" / "text-chat-owner-tests"
    evidence.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=evidence))
    work.chmod(0o700)
    (work / "home").mkdir(mode=0o700)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C.UTF-8",
           "LC_ALL": "C.UTF-8", "HOME": str(work / "home"), "TMPDIR": str(work)}
    network = repo / "port" / "network"
    owner = network / "optional" / "text_chat_owner.cpp"
    inputs = [network / "text_chat.h", network / "text_chat_owner.h",
              network / "optional" / "text_chat.cpp", owner,
              network / "tests" / "text_chat_owner_tests.cpp", Path(__file__).resolve()]
    receipt = {"schema": 1, "source_sha256": {
        str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in inputs}, "cases": [], "controls": [],
        "limits": ["invented offline fixtures only", "no production wiring acceptance",
                   "no game, provider, real peer or native Windows execution"]}

    def run_case(label, width, source, control=False):
        binary = work / (label + "-" + width)
        log = work / (binary.name + ".log")
        command = [args.compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-O0", "-g0", "-m" + width, "-fsanitize=undefined",
                   "-fno-sanitize-recover=all", str(network / "optional" / "text_chat.cpp"),
                   str(source), str(network / "tests" / "text_chat_owner_tests.cpp"),
                   "-o", str(binary)]
        result = {"label": label, "word_size": width, "compiled": False,
                  "executed": False, "passed": False, "log": str(log)}
        try:
            with log.open("wb") as output:
                compiled = subprocess.run(command, env=env, stdout=output, stderr=output,
                                          timeout=60, preexec_fn=limits, check=False)
                result["compile_exit"] = compiled.returncode
                result["compiled"] = compiled.returncode == 0
                if compiled.returncode != 0:
                    return result
                executed = subprocess.run([str(binary)], env=env, stdout=output, stderr=output,
                                          timeout=10, preexec_fn=limits, check=False)
                result["executed"] = True
                result["run_exit"] = executed.returncode
            match = re.search(r"^checks=(\d+) failures=(\d+)$", log.read_text(), re.MULTILINE)
            if match:
                result["checks"], result["failures"] = map(int, match.groups())
                result["passed"] = (executed.returncode == (1 if control else 0) and
                                    (result["failures"] > 0 if control else
                                     result["checks"] > 0 and result["failures"] == 0))
        except subprocess.TimeoutExpired:
            result["timeout"] = True
        except OSError:
            result["launch_error"] = True
        return result

    widths = ("32", "64") if args.word_size == "both" else (args.word_size,)
    for width in widths:
        receipt["cases"].append(run_case("owner", width, owner))

    if not args.no_controls and all(case["passed"] for case in receipt["cases"]):
        original = owner.read_text()
        mutations = {
            "epoch-retains-text": (
                "if (result == Result::Ok) {\n        ClearPresentation();\n        binding_.epoch",
                "if (result == Result::Ok) {\n        binding_.epoch"),
            "mute-displays-text": ("if (!muted_ && !suspended_) AddHistory",
                                   "if (!suspended_) AddHistory"),
            "retired-generation-reopened": (
                "if (binding.generation <= highest_generation_) return Result::Stale;", ""),
            "sync-sends-queued-head": (
                "Result Owner::PeekOutgoing(Outgoing& output) const {\n"
                "    Clear(output);\n    if (!queue_.IsOpen()) return Result::NotOpen;\n"
                "    if (suspended_) return Result::Backpressure;",
                "Result Owner::PeekOutgoing(Outgoing& output) const {\n"
                "    Clear(output);\n    if (!queue_.IsOpen()) return Result::NotOpen;"),
        }
        for label, (before, after) in mutations.items():
            if original.count(before) != 1:
                receipt["controls"].append({"label": label, "passed": False,
                                             "mutation_anchor_error": True})
                continue
            mutant = work / label / "network" / "optional"
            mutant.mkdir(parents=True)
            for header in ("text_chat.h", "text_chat_owner.h"):
                shutil.copyfile(network / header, mutant.parent / header)
            source = mutant / "text_chat_owner.cpp"
            source.write_text(original.replace(before, after, 1))
            for width in widths:
                receipt["controls"].append(run_case(label, width, source, control=True))

    receipt["passed"] = all(case["passed"] for group in ("cases", "controls")
                            for case in receipt[group])
    result_path = work / "result.private.json"
    result_path.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "positive_cases": len(receipt["cases"]),
                      "control_cases": len(receipt["controls"]), "receipt": str(result_path)}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
