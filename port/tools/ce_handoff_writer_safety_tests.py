#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline synthetic checks for the handoff writer; no game is started."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def build(directory: Path, flags: list[str], source=None) -> Path:
    binary, sha = directory / "writer-test", directory / "sha256.o"
    subprocess.run(["gcc", *flags, "-O1", "-c", str(ROOT / "port/updater/sha256.c"), "-o", str(sha)],
                   check=True, timeout=30)
    subprocess.run([
        "g++", *flags, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "port/platform/common"),
        str(ROOT / "port/tools/tests/ce_handoff_writer_safety_driver.cpp"),
        str(source if source is not None else ROOT / "port/platform/common/ce_netcode.cpp"), str(sha),
        "-Wl,--wrap=write", "-Wl,--wrap=ftruncate", "-Wl,--wrap=close", "-o", str(binary),
    ], check=True, timeout=30)
    print("HANDOFF_WRITER_FIXTURE_COMPILED", flush=True)
    return binary


def run(binary: Path, mode: str, path: Path) -> int:
    return subprocess.run([str(binary), mode, str(path)], input=b"x", stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, timeout=10, check=False).returncode


def race(binary: Path, path: Path) -> list[int]:
    read_fd, write_fd = os.pipe()
    children = []
    try:
        for _ in range(2):
            children.append(subprocess.Popen([str(binary), "stage", str(path)], stdin=read_fd,
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        os.close(read_fd)
        read_fd = -1
        os.write(write_fd, b"xx")
        os.close(write_fd)
        write_fd = -1
        return sorted(child.wait(timeout=10) for child in children)
    finally:
        if read_fd >= 0:
            os.close(read_fd)
        if write_fd >= 0:
            os.close(write_fd)


def digest(path: Path) -> bytes:
    return hashlib.sha256(path.read_bytes()).digest()


def require(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)


def causal_controls(m32):
    import hashlib
    import json
    import re
    source = ROOT / "port/platform/common/ce_netcode.cpp"
    text = source.read_text()
    truncate = r"ftruncate\(\s*fd\s*,\s*0\s*\)"
    require(len(re.findall(truncate, text)) == 1, "unique writer truncate control")
    mutations = [
        ("restore-path-delete", re.sub(truncate, "(ftruncate(fd,0), unlink(path))", text),
         "swap", "replacement preserved on write failure"),
        ("remove-owned-truncate", re.sub(truncate, "0", text),
         "all", "short write cleared through owned handle"),
    ]
    evidence_root = ROOT / "local-data/port/ce-handoff-writer-safety-controls"
    evidence_root.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="run-", dir=evidence_root))
    with tempfile.TemporaryDirectory(prefix="synthetic-handoff-controls-") as temp:
        directory = Path(temp)
        for label, changed, case, expected in mutations:
            # Keep these private copies bound to the real headers after relocation.
            changed = re.sub(r'^([ \t]*#include[ \t]+)"([^"\n]+)"',
                             lambda match: match[1] + '"' +
                             str((source.parent / match[2]).resolve()) + '"',
                             changed, flags=re.MULTILINE)
            candidate = directory / (label + ".cpp")
            candidate.write_text(changed)
            command = [sys.executable, str(Path(__file__).resolve()),
                       "--source", str(candidate), "--case", case]
            if m32:
                command.append("--m32")
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (evidence / (label + ".private.log")).write_text(result.stdout + result.stderr)
            (evidence / (label + ".json")).write_text(json.dumps({
                "source_sha256": hashlib.sha256(changed.encode()).hexdigest(),
                "m32": m32,
                "compiled": "HANDOFF_WRITER_FIXTURE_COMPILED" in result.stdout,
                "exit_code": result.returncode,
                "intended_assertion_observed": expected in result.stdout + result.stderr,
            }, indent=2) + "\n")
            require("HANDOFF_WRITER_FIXTURE_COMPILED" in result.stdout,
                    label + " compiled before its negative control")
            require(result.returncode != 0 and expected in result.stdout + result.stderr,
                    label + " is rejected by its intended assertion")
    print("handoff writer causal controls: 2 compiled, 2 rejected")
    print("private causal evidence: " + str(evidence))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m32", action="store_true", help="Compile and run a 32-bit Linux fixture.")
    parser.add_argument("--case", choices=("all", "collision", "swap", "incomplete"), default="all")
    parser.add_argument("--source", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        raise RuntimeError("synthetic writer tests currently require Linux")
    with tempfile.TemporaryDirectory(prefix="synthetic-handoff-writer-") as tmp:
        root = Path(tmp)
        os.chmod(root, 0o700)
        binary = build(root, ["-m32"] if args.m32 else [], args.source)
        if args.case in ("all", "collision"):
            existing = root / "existing"
            existing.write_bytes(b"authored unrelated fixture")
            before = digest(existing)
            require(run(binary, "stage", existing) == 1 and digest(existing) == before,
                    "occupied destination preserved")
            target, link = root / "target", root / "link"
            target.write_bytes(b"authored target fixture")
            link.symlink_to(target.name)
            before = digest(target)
            require(run(binary, "stage", link) == 1 and link.is_symlink() and digest(target) == before,
                    "occupied symlink and target preserved")
        if args.case in ("all", "swap"):
            path = root / "swap"
            require(run(binary, "swap", path) == 1 and path.is_file() and
                    path.read_bytes() == b"authored replacement fixture",
                    "replacement preserved on write failure")
        if args.case in ("all", "incomplete"):
            path = root / "incomplete"
            require(run(binary, "incomplete", path) == 1 and path.is_file(),
                    "incomplete destination retained")
        if args.case == "all":
            path = root / "fresh"
            require(run(binary, "stage", path) == 0 and stat.S_IMODE(path.stat().st_mode) == 0o600,
                    "fresh private handoff created")
            concurrent = root / "concurrent"
            require(race(binary, concurrent) == [0, 1], "exactly one concurrent writer succeeds")
            short = root / "short"
            require(run(binary, "short", short) == 1 and short.is_file() and short.stat().st_size == 0,
                    "short write cleared through owned handle")
            require(run(binary, "stage", short) != 0 and short.exists() and short.stat().st_size == 0,
                    "retained failed handoff blocks retry without replacement")
            truncate_fail = root / "truncate-fail"
            require(run(binary, "truncate-fail", truncate_fail) == 1 and truncate_fail.is_file() and
                    truncate_fail.stat().st_size > 0 and stat.S_IMODE(truncate_fail.stat().st_mode) == 0o600,
                    "failed truncation retains private incomplete file")
            close_fail = root / "close-fail"
            require(run(binary, "close-fail", close_fail) == 1 and close_fail.is_file() and
                    close_fail.stat().st_size > 0 and stat.S_IMODE(close_fail.stat().st_mode) == 0o600,
                    "late close failure retains private completed file")
    print("Handoff writer synthetic tests: PASS (Linux only; no game or network)")
    if args.source is None and args.case == "all":
        causal_controls(args.m32)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
