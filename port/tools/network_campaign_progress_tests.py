#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile and test the isolated, in-memory campaign result component.

This uses only the named component and synthetic driver. It does not load the
game, run the patch/build pipeline, read careers or create a network session.
"""

from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    game = root / "port" / "game"
    source = game / "ce_network_campaign.cpp"
    header = game / "ce_network_campaign.h"
    driver = root / "port" / "tools" / "tests" / "network_campaign_progress_driver.cpp"
    compiler = shutil.which("c++")
    if not compiler or not all(p.is_file() for p in (source, header, driver)):
        print("campaign component: required input unavailable", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="ce-campaign-component-") as tmp:
        tmp_path = Path(tmp)
        executable = tmp_path / "campaign-component-test"
        env = {"HOME": tmp, "TMPDIR": tmp, "PATH": os.defpath, "LC_ALL": "C"}
        command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                   "-pedantic", "-I", str(game), str(source), str(driver),
                   "-o", str(executable)]
        try:
            built = subprocess.run(command, cwd=tmp, env=env, capture_output=True,
                                   timeout=30, check=False)
            if built.returncode != 0:
                print("campaign component: compile failed", file=sys.stderr)
                return 1
            ran = subprocess.run([str(executable)], cwd=tmp, env=env,
                                 capture_output=True, timeout=30, check=False)
        except (OSError, subprocess.TimeoutExpired):
            print("campaign component: compiler or test did not finish", file=sys.stderr)
            return 1
        marker = ran.stdout.decode("ascii", errors="replace").strip()
        if ran.returncode != 0 or not re.fullmatch(
                r"campaign component: [1-9][0-9]* synthetic checks passed", marker):
            print("campaign component: synthetic checks failed", file=sys.stderr)
            return 1
        print(marker)
    return 0


if __name__ == "__main__":
    sys.exit(main())
