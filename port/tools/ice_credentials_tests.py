#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/run authored local grant checks; never contacts a relay or provider."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--juice-include", type=Path)
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--word-size", choices=("32", "64"))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    base = args.out or root / "local-data" / "ice-credentials-tests"
    base.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=base)).resolve()
    output.chmod(0o700)
    include = args.juice_include
    if include is None:
        include = output / "fixture-include"
        (include / "juice").mkdir(parents=True)
        (include / "juice" / "juice.h").write_text(
            "#include <stdint.h>\n"
            "typedef struct { const char *host,*username,*password; uint16_t port; } juice_turn_server_t;\n"
            "typedef struct { const char *stun_server_host; uint16_t stun_server_port; "
            "juice_turn_server_t *turn_servers; int turn_servers_count; } juice_config_t;\n"
        )
    executable = output / ("checks.exe" if "mingw" in args.cxx else "checks")
    command = ["nice", "-n", "10", args.cxx, "-std=c++17", "-O0", "-g0", "-Wall", "-Wextra", "-Werror"]
    if args.word_size:
        command.append("-m" + args.word_size)
    command += ["-I", str(include), "-I", str(root / "port/network"), str(root / "port/network/tests/ice_credentials_tests.cpp"), "-o", str(executable)]
    result = {"include": "pinned-header" if args.juice_include else "authored-interface-fixture", "compile_only": args.compile_only}
    for label, command in [("compile", command)] + ([] if args.compile_only else [("run", [str(executable), str(output)])]):
        run = subprocess.run(command, capture_output=True, timeout=60 if label == "compile" else 10)
        (output / (label + ".stdout.log")).write_bytes(run.stdout)
        (output / (label + ".stderr.log")).write_bytes(run.stderr)
        result[label + "_rc"] = run.returncode
        if run.returncode:
            (output / "result.private.json").write_text(json.dumps(result, indent=2) + "\n")
            print("ice credential checks failed; private diagnostics retained")
            return 1
    (output / "result.private.json").write_text(json.dumps(result, indent=2) + "\n")
    print("ice credential checks passed" + (" (compile only)" if args.compile_only else ""))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError):
        print("ice credential checks could not complete")
        raise SystemExit(1)
