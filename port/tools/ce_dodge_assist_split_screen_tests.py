#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Dodge Button in split screen: ce.ini is read on the way into a level, not while one player waits to come back.

    python3 -I port/tools/ce_dodge_assist_split_screen_tests.py [--compiler g++]

port/game/ce_dodge_assist.cpp reads the dodge_button line of ce.ini when a controller next drives a Battle Engine after an
update in which it drove none. That memory was one flag for all four pads. In two-player split screen a player who is lost
waits five seconds to come back; for part of that wait their controller drives no engine while the other player's does, so
the first raised the flag and the second answered it in every controller update (twenty a second), each answer a read of the
whole of ce.ini (on Windows, of every folder on its path as well). The flag is now each pad's own.

The fixture is ce_dodge_assist_tests.py's (the actual ce_dodge_assist.cpp and CController::DoMappings as patch 0220 leaves
it, beside the game's own strafe code, against a synthetic game whose ce.ini counts its reads), built with
port/tools/ce_dodge_assist_split_screen_driver.cpp, which adds scripts for two controllers. Not in the fixture: the game's own
player, whose engine is gone during the wait (the fixture plays the wait as a controller that drives no player; the text
check below holds the two to the same answer), a real ce.ini and its cost, and a running game.

With the row On the end hook asks; with it Off the filter hook asks as well (the assist is idle), so a filter that does not name its pad is seen, and both are played with one
player waiting.

Mutants: the flag shared again, and each hook forgetting to say whose update it is. Each must change a line of the output or
is reported as surviving with its reason.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
sys.path.insert(0, str(ROOT / "port/tools"))
import build  # noqa: E402
import ce_dodge_assist_tests as fixture  # noqa: E402
import quiet_crash  # noqa: E402

DRIVER = ROOT / "port/tools/ce_dodge_assist_split_screen_driver.cpp"
GLUE = ROOT / "port/game/ce_dodge_assist.cpp"

# mode: the driver's exact output
WANT = {
    "one-player-waits": [
        "in the level: 2 reads of ce.ini, 1 and 1 dashes",
        "while player two waits, 100 updates: 0 reads of ce.ini, 2 dashes of player one",
        "player two is back, 40 updates: 1 reads of ce.ini, 1 dashes of player two",
    ],
    "player-one-waits": [
        "while player one waits, 100 updates: 0 reads of ce.ini, 2 dashes of player two",
    ],
    "off-player-one-waits": [
        "off, in the level: 2 reads of ce.ini, 0 and 0 dashes",
        "off, while player one waits and player two strafes, 100 updates: 0 reads of ce.ini",
    ],
    "off-player-two-waits": [
        "off, in the level: 2 reads of ce.ini, 0 and 0 dashes",
        "off, while player two waits and player one strafes, 100 updates: 0 reads of ce.ini",
    ],
    "setting-changed-in-the-front-end": [
        "on: 1 and 1 dashes",
        "off in the front end: 0 and 0 dashes, 2 reads of ce.ini",
        "on again: 1 and 1 dashes, 2 reads of ce.ini",
    ],
    "help-asks-first": [
        "the help, before any level: on, 1 reads of ce.ini",
        "off in the front end: 0 and 0 dashes, 2 reads of ce.ini",
    ],
    "one-player": [
        "one player, a level: 1 reads of ce.ini, 1 dashes",
        "one player, the next level: 2 reads of ce.ini in all, 1 dashes",
    ],
}

# name: (text that occurs once in ce_dodge_assist.cpp, its replacement, the modes that must change; empty: reported if it survives)
MUTANTS = {
    "the four pads share one flag again": ("#define gSettingStale gStale[gPad]", "#define gSettingStale gStale[0]", ("one-player-waits", "player-one-waits")),
    "the end hook does not say whose update it is": ("\tCeDodge::Update u = {};\n\tgPad = pad;", "\tCeDodge::Update u = {};", ("one-player-waits",)),
    "the filter hook does not say whose update it is": ("\tState &s = gState[pad];\n\tgPad = pad;\n\tconst bool active", "\tState &s = gState[pad];\n\tconst bool active",
                                                        ("off-player-one-waits", "off-player-two-waits")),
    "a pad starts as if it had already asked": ("bool gStale[kPads] = {true, true, true, true};", "bool gStale[kPads] = {true, false, false, false};", ("one-player-waits",)),
}


def make(folder, texts, compiler, glue=None):
    files = fixture.pieces(texts, True)
    if glue is not None:
        files["dodge_glue.inc"] = re.sub(r"^#include\s+.*$", "", glue, flags=re.M).replace("__attribute__((weak))", "")
    folder = Path(folder)
    for name, value in files.items():
        (folder / name).write_text(value)
    binary = folder / "fixture"
    done = subprocess.run([compiler, "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare", "-Wno-unused-function",
                           "-fno-diagnostics-show-caret", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(folder), "-I" + str(ROOT / "port/game"),
                           "-I" + str(ROOT / "port/tools"), '-DCE_VERSION="fixture"', str(DRIVER), "-o", str(binary)], capture_output=True, text=True, timeout=300)
    if done.returncode:
        raise RuntimeError("the fixture did not build:\n" + "\n".join(l for l in (done.stdout + done.stderr).splitlines() if "error" in l)[:3000])
    return binary


def run(binary, mode):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    done = quiet_crash.run([str(binary), mode], capture_output=True, text=True, env=env, timeout=120)     # a mutant that faults leaves no core dump
    return done.returncode, done.stdout.splitlines(), done.stderr


def text_problems(glue):
    """What is wrong with the two places the fixture cannot play: the engine the game's player gives, and the wait's length."""
    problems = []
    if not re.search(r"if \(player && controller->GetToControl\(\) == player\)\s*\n\s*return player->GetBattleEngine\(\);", glue):
        problems.append("ce_dodge_assist.cpp: Driven() no longer answers with the player's own engine (none while the player waits to come back)")
    header = (ROOT / "src/player.h").read_text(encoding="latin-1")
    if not re.search(r"CBattleEngine\s*\*\s*GetBattleEngine\(\)\s*\{\s*return mBattleEngine\.ToRead\(\)\s*;\s*\}", header):
        problems.append("player.h: the player's engine is no longer an active reader's (NULL once the engine is deleted)")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--glue", type=Path, help="another text of ce_dodge_assist.cpp to judge (the modes only)")
    args = parser.parse_args()
    failures = 0
    texts = build.patched_text(["Controller.h", "Controller.cpp"], network=False)
    glue = (args.glue or GLUE).read_text()
    for problem in text_problems(glue):
        print("FAIL " + problem)
        failures += 1
    parent = ROOT / "local-data/port/ce-dodge-assist-split-screen-tests"
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="run-", dir=parent) as out:
        folder = Path(out) / "real"
        folder.mkdir()
        binary = make(folder, texts, args.compiler, glue)
        for mode, want in WANT.items():
            code, got, err = run(binary, mode)
            ok = code == 0 and got == want
            failures += not ok
            print(("PASS " if ok else "FAIL ") + mode)
            if not ok:
                for line in got:
                    print("     got: " + line)
                if code:
                    print(f"     exit {code}: {err.strip()[:300]}")
        if args.glue is None:
            for name, (old, new, modes) in MUTANTS.items():
                if glue.count(old) != 1:
                    print(f"FAIL mutant: {name}: the anchor must occur once ({glue.count(old)})")
                    failures += 1
                    continue
                folder = Path(out) / re.sub(r"\W+", "-", name)
                folder.mkdir()
                try:
                    mutant = make(folder, texts, args.compiler, glue.replace(old, new))
                except RuntimeError:
                    print(f"PASS mutant: {name}: does not build")
                    continue
                changed = [mode for mode, want in WANT.items() if run(mutant, mode)[:2] != (0, want)]
                ok = bool(changed) and all(mode in changed for mode in modes)
                failures += not ok
                print(("PASS " if ok else "FAIL ") + f"mutant: {name}: changes {', '.join(changed) or 'nothing'}")
    print("dodge button split-screen check: " + ("OK" if not failures else f"{failures} FAILED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
