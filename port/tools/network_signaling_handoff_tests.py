#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authored Linux authority/exec controls; no game, service, real token or held lobby driver."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise RuntimeError("causal control no longer identifies one guard")
    return source.replace(before, after, 1)


def replace_body(source: str, name: str, replacement: str) -> str:
    match = re.search(r"bool " + name + r"\([^{}]*\) noexcept \{", source)
    if match is None:
        raise RuntimeError("causal control function was not found")
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:match.end()] + replacement + source[end - 1:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--causal-controls", action="store_true")
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("owned native-handle acceptance is currently Linux only")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error("the requested authored-test compiler is unavailable")
    parent = ROOT / "local-data/port/qa"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="signal-authority-", dir=parent))
    env = {key: value for key, value in os.environ.items() if key not in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS")}
    print(f"Authority fixture output: {output}", flush=True)

    def run(label: str, source: Path) -> subprocess.CompletedProcess[str]:
        binary = output / label
        command = [compiler, "-std=gnu++14", "-DCE_NETWORK_ICE", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread",
                   "-I", str(ROOT / "port"), "-I", str(ROOT / "port/game"),
                   str(ROOT / "port/tools/tests/network_signaling_handoff_driver.cpp"), str(source), "-o", str(binary)]
        built = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (output / (label + "-compile.log")).write_text(built.stdout)
        if built.returncode:
            for line in built.stdout.splitlines():
                if re.search(r": (?:fatal )?(?:error|warning):", line):
                    print(line, file=sys.stderr)
            raise RuntimeError("authored control did not compile: " + label)
        runtime = output / (label + "-fixtures")
        runtime.mkdir(mode=0o700)
        result = subprocess.run([str(binary), str(runtime)], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / (label + "-run.log")).write_text(result.stdout)
        return result

    source = ROOT / "port/game/ce_network_boot_authority.cpp"
    result = run("authority-tests", source)
    print(result.stdout, end="")
    if result.returncode or not re.search(r"SIGNAL_HANDOFF_TESTS checks=[1-9][0-9]* failures=0", result.stdout):
        return 1
    if args.causal_controls:
        original = source.read_text()
        mutants = {
            "seals": replace_once(original, "seals >= 0 && (seals & RequiredSeals) == RequiredSeals", "((void)seals, true)"),
            "context": replace_once(original, "Same(request.context, state_->plan.context, sizeof state_->plan.context)", "true"),
            "role": replace_once(original, "request.role == state_->pair.role", "true"),
            "deadlines": replace_body(original, "Times", "(void)plan; (void)pairExpiry; (void)utc; (void)mono; return true;"),
            "directory": replace_body(original, "DirectoryMatches", "(void)path; (void)expected; return true;"),
            "consume-once": replace_once(original, "lseek(descriptor, 0, SEEK_CUR) != 0", "lseek(descriptor, 0, SEEK_SET) < 0"),
            "seal-once": replace_once(original, "state_->bound || state_->sealed || state_->terminal", "state_->bound || state_->terminal"),
        }
        for label, text in mutants.items():
            mutated = output / (label + ".cpp")
            mutated.write_text(text)
            witness = run(label, mutated)
            if witness.returncode == 0 or not re.search(r"SIGNAL_HANDOFF_TESTS checks=[1-9][0-9]* failures=[1-9]", witness.stdout):
                raise RuntimeError("causal control was not rejected by a completed fixture: " + label)
        print(f"Causal controls: {len(mutants)} compiled, {len(mutants)} rejected by runtime assertions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
