#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check production bridge loss accounting with authored dependency I/O.

Only verified header archives are extracted; no dependency code, sockets,
provider, credentials or game data is used. Windows is compile/link only.
Use a fresh ignored output directory and ordinary bounded build coordination.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import ice_bridge_build as bridge

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/network/ice_bridge.cpp"
HEADER = ROOT / "port/network/ice_bridge.h"
DRIVER = ROOT / "port/network/tests/ice_bridge_diagnostics_tests.cpp"
COMPLETION = re.compile(r"PASS ([1-9][0-9]*) authored ICE bridge diagnostic checks\n")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError("diagnostic control anchor is no longer unique")
    return text.replace(before, after)


def run(args: argparse.Namespace) -> dict:
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to((ROOT / "local-data").resolve()):
        raise ValueError("output must be new and below this checkout's local-data")
    if args.target != "native" and not args.toolchain_data:
        raise ValueError("cross builds require existing pinned toolchain data")
    if args.target == "windows32" and args.run:
        raise ValueError("Windows execution is a separate acceptance step")
    if args.causal and not args.run:
        raise ValueError("causal controls require an executed authored baseline")
    os.umask(0o077)
    output.mkdir(mode=0o700, parents=True)
    inputs = output / "inputs"
    inputs.mkdir(mode=0o700)
    bridge.unpack(args.juice_archive, bridge.JUICE_SHA256, inputs)
    bridge.unpack(args.enet_archive, bridge.ENET_SHA256, inputs)
    juice = inputs / ("libjuice-" + bridge.JUICE_COMMIT)
    enet = inputs / ("enet-" + bridge.ENET_COMMIT)
    if sha256(bridge.JUICE_PATCH) != bridge.JUICE_PATCH_SHA256:
        raise ValueError("relay patch identity changed")
    patched = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(bridge.JUICE_PATCH)],
                             cwd=juice, capture_output=True, timeout=15)
    (output / "header-patch.private.log").write_bytes(patched.stdout + patched.stderr)
    if patched.returncode:
        raise ValueError("pinned headers could not be prepared")

    data = args.toolchain_data.resolve() if args.toolchain_data else None
    if args.target == "windows32":
        cxx = data / "llvm-mingw/bin/i686-w64-mingw32-clang++"
        flags = []
    elif data:
        cxx = data / "llvm/bin/clang++"
        flags = ["-m32", "--sysroot=" + str(data / "sysroot-sniper")] if args.target == "linux32" else []
    else:
        cxx = shutil.which("c++")
        flags = []
    if not cxx:
        raise ValueError("C++ compiler unavailable")
    common = [str(cxx), *flags, "-std=c++17", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror"]
    optional = ["-DCE_NETWORK_ICE=1", "-DJUICE_STATIC", "-I" + str(juice / "include"),
                "-I" + str(enet / "include"), "-I" + str(ROOT / "port/network")]
    suffix = ".exe" if args.target == "windows32" else ""
    record = {"schema": 1, "target": args.target, "game_process": False,
              "network_io": "authored substitutes only; no sockets or real dependency agents",
              "executed": bool(args.run), "results": [],
              "source_sha256": {str(path.relative_to(ROOT)): sha256(path) for path in (
                  SOURCE, HEADER, DRIVER, ROOT / "port/network/tests/fixture_guard.h",
                  Path(__file__).resolve(), ROOT / "port/tools/ice_bridge_build.py")},
              "archives": {"libjuice": bridge.JUICE_SHA256, "enet": bridge.ENET_SHA256},
              "relay_patch_sha256": bridge.JUICE_PATCH_SHA256}

    def save() -> None:
        (output / "result.json").write_text(json.dumps(record, indent=2) + "\n")

    def compile(command: list[str | Path], label: str) -> int:
        with (output / (label + "-build.private.log")).open("wb") as stream:
            result = subprocess.run(list(map(str, command)), stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=90)
        return result.returncode

    disabled = compile([*common, "-c", SOURCE, "-o", output / "disabled-bridge.o"], "disabled")
    record["disabled_build_exit"] = disabled
    save()
    if disabled:
        raise ValueError("disabled bridge failed to compile")

    def trial(label: str, source: Path, assertion: str = "") -> bool:
        binary = output / (label + suffix)
        row = {"name": label, "expected_assertion": assertion,
               "source_sha256": sha256(source),
               "build_exit": compile([*common, *optional, source, DRIVER, "-o", binary], label)}
        if row["build_exit"]:
            row["passed"] = False
        else:
            row["binary_sha256"] = sha256(binary)
            if args.run:
                result = subprocess.run([str(binary)], capture_output=True, timeout=10)
                (output / (label + "-run.private.log")).write_bytes(result.stdout + result.stderr)
                stdout = result.stdout.decode("ascii", errors="replace")
                stderr = result.stderr.decode("ascii", errors="replace")
                marker = COMPLETION.fullmatch(stdout)
                row["exit_code"] = result.returncode
                row["completed"] = bool(marker)
                row["checks"] = int(marker[1]) if marker else 0
                if assertion:
                    row["intended_assertion_observed"] = stderr == "FAIL " + assertion + "\n"
                    row["passed"] = result.returncode == 1 and not marker and row["intended_assertion_observed"]
                else:
                    row["passed"] = result.returncode == 0 and bool(marker) and not stderr
            else:
                row["passed"] = True
        record["results"].append(row)
        save()
        return row["passed"]

    okay = trial("actual", SOURCE)
    if okay and args.causal:
        text = SOURCE.read_text()
        controls = [
            ("omit-overflow", "CountLoss(bridge->losses_.queue_overflow);", "(void)0;",
             "sixty-fifth-callback-counts-one-overflow"),
            ("omit-ice-forwarding", "CountLoss(losses_.ice_send);", "(void)0;",
             "ice-forwarding-failure-counted-once"),
            ("omit-local-delivery", "CountLoss(losses_.local_delivery);", "(void)0;",
             "local-forwarding-failure-counted-once"),
            ("fatal-overflow", "CountLoss(bridge->losses_.queue_overflow);",
             "{ CountLoss(bridge->losses_.queue_overflow); bridge->failed_.store(true); }",
             "overflow-preserves-ordinary-udp-loss-behavior"),
            ("fatal-ice-forwarding", "CountLoss(losses_.ice_send);",
             "CountLoss(losses_.ice_send); failed_.store(true);",
             "ice-forwarding-loss-is-nonfatal-and-recovers"),
            ("fatal-local-delivery", "CountLoss(losses_.local_delivery);",
             "CountLoss(losses_.local_delivery); failed_.store(true);",
             "local-forwarding-loss-is-nonfatal"),
        ]
        for label, before, after, assertion in controls:
            selected = output / (label + ".cpp")
            selected.write_text(replace_once(text, before, after))
            okay = trial(label, selected, assertion) and okay
    record["passed"] = okay
    record["limits"] = ["Deterministic production bridge checks with authored I/O; no real relay or game.",
                        "No session teardown persistence, native Windows execution or player acceptance.",
                        "Counter saturation is source-inspected; fixtures do not perform 2^64 events."]
    save()
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("juice-archive", "enet-archive", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--target", choices=("native", "linux32", "windows32"), default="native")
    parser.add_argument("--toolchain-data", type=Path)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--causal", action="store_true")
    args = parser.parse_args()
    try:
        result = run(args)
    except (ValueError, OSError, subprocess.SubprocessError):
        raise SystemExit("bridge diagnostics check refused; private evidence retained") from None
    if not result["passed"]:
        raise SystemExit("bridge diagnostics check failed; private evidence retained")
    print("PASS authored bridge diagnostics:", args.target,
          "executed" if args.run else "compile/link only")


if __name__ == "__main__":
    main()
