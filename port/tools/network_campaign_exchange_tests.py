#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test the disconnected result-exchange prototype with an in-memory pump.

Only the named component sources and synthetic driver are read. This does not
run the game, patch/build pipeline, a network session, or career operations.
"""

from pathlib import Path
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    game = root / "port" / "game"
    inputs = [game / "ce_network_campaign.cpp", game / "ce_network_campaign.h",
              game / "ce_network_result_exchange.cpp", game / "ce_network_result_exchange.h",
              root / "port" / "tools" / "tests" / "network_campaign_exchange_driver.cpp"]
    compiler = shutil.which("c++")
    if not compiler or not all(p.is_file() for p in inputs):
        print("exchange prototype: required input unavailable", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="ce-result-exchange-") as tmp:
        executable = Path(tmp) / "exchange-test"
        env = {"HOME": tmp, "TMPDIR": tmp, "PATH": os.defpath, "LC_ALL": "C"}
        command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-I", str(game), str(inputs[0]), str(inputs[2]), str(inputs[4]),
                   "-o", str(executable)]
        try:
            built = subprocess.run(command, cwd=tmp, env=env, capture_output=True,
                                   timeout=30, check=False)
            if built.returncode != 0:
                print("exchange prototype: compile failed", file=sys.stderr)
                return 1
            ran = subprocess.run([str(executable)], cwd=tmp, env=env,
                                 capture_output=True, timeout=30, check=False)
            fixture = subprocess.run([str(executable), "--codec-fixture"], cwd=tmp, env=env,
                                     capture_output=True, timeout=30, check=False)
        except (OSError, subprocess.TimeoutExpired):
            print("exchange prototype: compiler or test did not finish", file=sys.stderr)
            return 1
        marker = ran.stdout.decode("ascii", errors="replace").strip()
        if ran.returncode != 0 or not re.fullmatch(
                r"exchange prototype: [1-9][0-9]* synthetic checks passed", marker):
            print("exchange prototype: synthetic checks failed", file=sys.stderr)
            return 1
        expected = struct.pack(
            ">BBBBQQIQQ", 1, 1, 1, 1, (1 << 60) + 12345, (1 << 63) + 67890,
            (1 << 28) + 2468, (1 << 62) + 12345, (1 << 63) + 67890)
        if fixture.returncode != 0 or fixture.stderr or fixture.stdout != expected:
            print("exchange prototype: independent codec fixture failed", file=sys.stderr)
            return 1
        print(marker)
    return 0


if __name__ == "__main__":
    sys.exit(main())
