#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A generated mirror in a scratch folder of the caller's choosing, optionally stopped before a patch number.

    python3 port/tools/premirror.py OUT                # the full mirror: every patch applied
    python3 port/tools/premirror.py OUT --below 74     # the pre-0074 mirror: patches 0074 and above are left out

Suites that need a mirror but must start from the unpatched text of one patch (ce_frontend_tests applies 0074 itself)
build theirs with scratch_mirror(), so nobody needs a hand-made one. It runs port/build.py's own mirror builder with
only its output folder and its patch loop changed; port/build.py's default mirror is untouched. BEA_GAME_DIR must name
the game data the build pins (config/game-data.sha256), as for every mirror.
"""
import argparse
import re
import subprocess
import sys
import types
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import build


def scratch_mirror(out, below=None):
    """Build the mirror under OUT (OUT/src and OUT/linklib) and return OUT/src; patches numbered BELOW and above are skipped."""
    out = Path(out)
    real = subprocess.run

    def run(cmd, *args, **kwargs):
        if below is not None and cmd and cmd[0] == "patch":
            m = re.match(r"(\d+)-", Path(cmd[cmd.index("-i") + 1]).name)
            if m and int(m.group(1)) >= below:
                return subprocess.CompletedProcess(cmd, 0, "", "")
        return real(cmd, *args, **kwargs)

    saved = build.MIRROR_ROOT, build.MIRROR, build.subprocess
    build.MIRROR_ROOT, build.MIRROR = out, out / "src"
    build.subprocess = types.SimpleNamespace(**{k: getattr(subprocess, k) for k in dir(subprocess)
                                                if not k.startswith("_") and k != "run"}, run=run)
    try:
        build.build_mirror()
    finally:
        build.MIRROR_ROOT, build.MIRROR, build.subprocess = saved
    return out / "src"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path, help="the folder to build the mirror in (created)")
    ap.add_argument("--below", type=int, help="leave out patches numbered this and above")
    args = ap.parse_args()
    print("mirror", scratch_mirror(args.out, args.below))


if __name__ == "__main__":
    main()
