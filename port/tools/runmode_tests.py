#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The run mode (port/platform/common/runmode.h) on the host: no game data, no build.

Built as a test-kit build: every variable that hid the online pages before the run mode (PortCeNetOffered's own list) still
hides them alone; ordinary play offers them; a test-kit journey on a test clock (BEA_PORT_VERBS with BEA_PORT_CLOCK) offers
them, the one intended change; probes, tapes, recordings and network roles still hide them even with a journey's steps set.
Built as a player's build (no test hooks): no environment gives the test-kit mode; BEA_PORT_VERBS changes nothing, so with a
test clock the pages stay hidden, exactly as before the run mode."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD_GATE = ["BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT",
            "BEA_RECORD", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_LOCKSTEP_HOST", "BEA_LOCKSTEP_JOIN"]
CASES = [({}, "play", 1)] + [({name: "x"}, None, 0) for name in OLD_GATE] + [
    ({"BEA_PORT_VERBS": "steps.txt"}, "test-kit", 1),
    ({"BEA_PORT_VERBS": "steps.txt", "BEA_PORT_CLOCK": "fixed"}, "test-kit", 1),
    ({"BEA_PORT_VERBS": "steps.txt", "BEA_PORT_PROBES": "x"}, "test", 0),
    ({"BEA_PORT_VERBS": "steps.txt", "BEA_PORT_TAPE": "t"}, "replay", 0),
    ({"BEA_PORT_VERBS": "steps.txt", "BEA_PORT_RECORD": "r"}, "record", 0),
    ({"BEA_PORT_VERBS": "steps.txt", "BEA_NET_HOST": "1"}, "network-test", 0),
    ({"BEA_PORT_CLOCK": ""}, "play", 1),
]
# a player's build: the kit's variables change nothing
PLAYER_CASES = [({}, "play", 1), ({"BEA_PORT_VERBS": "steps.txt"}, "play", 1), ({"BEA_PORT_VERB_OUT": "o"}, "play", 1),
                ({"BEA_PORT_VERBS": "steps.txt", "BEA_PORT_CLOCK": "fixed"}, "test", 0),
                ({"BEA_PORT_VERBS": "s", "BEA_PORT_VERB_OUT": "o", "BEA_PORT_VERB_RUN_DIR": "d", "BEA_PORT_CLOCK": "fixed",
                  "BEA_PORT_CLOCK_DT": "0.016667"}, "test", 0)] + [({name: "x", "BEA_PORT_VERBS": "s"}, None, 0) for name in OLD_GATE]


def main():
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        sys.exit("a C++ compiler is required")
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        base = {k: v for k, v in os.environ.items() if not k.startswith("BEA_")}
        for label, extra, cases in (("kit", ["-DKIT_BUILD"], CASES), ("player", [], PLAYER_CASES)):
            exe = Path(tmp) / f"runmode-{label}"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *extra,
                            str(ROOT / "port/platform/common/runmode.cpp"), str(ROOT / "port/tools/tests/runmode_driver.cpp"),
                            "-o", str(exe)], check=True)
            for env, mode, offered in cases:
                out = subprocess.run([str(exe)], env={**base, **env}, capture_output=True, text=True, check=True).stdout.split()
                if (mode and out[0] != mode) or int(out[1]) != offered:
                    failures.append(f"{label} {env}: got {out}, wanted {mode or '(any)'} {offered}")
        # the player's runmode.cpp does not name the kit's variables at all
        if "BEA_PORT_VERB" in (ROOT / "port/platform/common/runmode.cpp").read_text():
            failures.append("runmode.cpp names a test-kit variable")
    print(json.dumps({"passed": not failures, "cases": len(CASES) + len(PLAYER_CASES), "failures": failures}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
