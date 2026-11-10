#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/run the offline text codec fixture; no game or networking dependency."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--word-size", type=int, choices=(32, 64))
    args = parser.parse_args()
    os.umask(0o077)
    root = Path(__file__).resolve().parents[2]
    parent = root / "local-data" / "text-chat-tests"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    sources = [root / "port/network/optional/text_chat.cpp",
               root / "port/network/tests/text_chat_tests.cpp"]
    binary = output / "text-chat-tests"
    command = [args.compiler, "-std=c++17", "-O0", "-g0", "-Wall", "-Wextra",
               "-Werror", "-Wpedantic", "-I", str(root / "port/network")]
    if args.word_size:
        command.append(f"-m{args.word_size}")
    command.extend([*(str(path) for path in sources), "-o", str(binary)])
    if os.name == "posix":
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = {"status": "compile_failed", "word_size": args.word_size,
              "sources": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in [root / "port/network/text_chat.h", *sources]}}
    try:
        with (output / "compile.log").open("wb") as log:
            compiled = subprocess.run(command, stdout=log, stderr=log, timeout=45,
                                      check=False)
        result["compile_exit"] = compiled.returncode
        if compiled.returncode == 0:
            with (output / "stdout.json").open("wb") as stdout, \
                    (output / "stderr.log").open("wb") as stderr:
                ran = subprocess.run([str(binary)], stdout=stdout, stderr=stderr,
                                     timeout=10, check=False)
            result["test_exit"] = ran.returncode
            result["status"] = "passed" if ran.returncode == 0 else "test_failed"
            try:
                result["checks"] = json.loads((output / "stdout.json").read_text())
            except (ValueError, UnicodeError):
                result["status"] = "invalid_test_summary"
    except (OSError, subprocess.TimeoutExpired):
        result["status"] = "execution_failed"
    receipt = output / "result.json"
    receipt.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "receipt": str(receipt)}, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
