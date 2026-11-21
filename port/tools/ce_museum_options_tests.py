#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The options a museum visit hands back to the player (host arithmetic, with mutants).

The visit gives the whole career record back as it found it, but an option the visitor changed in the pause menu is theirs to keep. The keep and restore code
(ce_museum_options.h) is a template over a career-shaped object, so this test compiles the very code the visit runs against an authored stand-in and checks the
behaviour field by field: every option comes back in its own field, the progress half is exactly as it was, nothing else is written. It also reads the game's own
Career.h and fails when the career gains an option setter the visit does not keep (or the visit keeps one the career lacks). Hand mutants of the header are each
caught by the driver; a mutant that survives fails this test. Nothing runs the game and no retail content is used.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_options.h"
DRIVER = ROOT / "port/tools/tests/ce_museum_options_driver.cpp"
CAREER = ROOT / "src/Career.h"
# What the career's setters that are not options are: progress, which a visit gives back, and the god cheat.
NOT_OPTIONS = {"BaseThingExistTo", "GoodieState", "InProgress", "Slot", "IsGod"}


def run(command, expect=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    return result.returncode == expect, result.stdout + result.stderr


MUTANTS = [
    ("music-kept-from-the-sound-volume", "kept.music = career.GetMusicVolume();", "kept.music = career.GetSoundVolume();"),
    ("sound-restored-into-the-music-volume", "career.SetSoundVolume(kept.sound);", "career.SetMusicVolume(kept.sound);"),
    ("music-not-restored", "career.SetMusicVolume(kept.music);\n", ""),
    ("second-player-takes-the-first", "career.SetVibration(player, kept.vibration[player]);", "career.SetVibration(player, kept.vibration[0]);"),
    ("jet-axis-restored-into-the-ordinary-axis", "career.SetInvertJetYAxis(player, kept.invertJet[player]);", "career.SetInvertYAxis(player, kept.invertJet[player]);"),
    ("controller-not-kept", "kept.controller[player] = career.GetControllerConfigurationNum(player);", ""),
    ("screen-x-from-y", "kept.screenX = career.GetScreenPosX();", "kept.screenX = career.GetScreenPosY();"),
    ("screen-y-not-restored", "career.SetScreenPosY(kept.screenY);", ""),
    ("invert-not-kept", "kept.invert[player] = career.GetInvertYAxis(player);", ""),
    ("only-one-player-restored", "for (int player = 0; player != 2; ++player)\n\t{\n\t\tcareer.SetInvertYAxis", "for (int player = 0; player != 1; ++player)\n\t{\n\t\tcareer.SetInvertYAxis"),
    ("progress-written-by-the-restore", "career.SetScreenPosX(kept.screenX);", "career.SetScreenPosX(kept.screenX); career.progress[0] = 0;"),
    ("keeping-writes-the-career", "kept.sound = career.GetSoundVolume();", "kept.sound = career.GetSoundVolume(); career.SetSoundVolume(0.0f);"),
    ("save-keeps-the-snapshot-options", "CeMuseumRestoreOptions(saved, CeMuseumKeepOptions(live));", "CeMuseumRestoreOptions(saved, CeMuseumKeepOptions(saved));"),
    ("save-writes-the-live-progress", "CeMuseumRestoreOptions(saved, CeMuseumKeepOptions(live));", "saved = live;"),
    ("save-keeps-the-snapshot-god-flags", "saved.SetIsGod(player, live.GetIsGod(player));", "saved.SetIsGod(player, saved.GetIsGod(player));"),
    ("save-writes-the-live-career", "saved.SetIsGod(player, live.GetIsGod(player));", "saved.SetIsGod(player, live.GetIsGod(player)), live.SetSoundVolume(0.0f);"),
]


def main():
    failures = []

    def check(name, passed, detail=""):
        print(("ok   " if passed else "FAIL ") + name + ("" if passed else ("\n" + detail if detail else "")))
        if not passed:
            failures.append(name)

    text = HEADER.read_text()
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        built, out = run(["g++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-I" + str(GAME), str(DRIVER), "-o", str(folder / "driver")])
        check("the options code compiles against the stand-in career", built, out)
        if built:
            good, out = run([str(folder / "driver")])
            check("the options code keeps and restores every field, and nothing else", good, out)
        for label, old, new in MUTANTS:
            if text.count(old) != 1:
                check("mutant " + label + " applies", False, "source text not found once: " + old)
                continue
            mutant = folder / label
            mutant.mkdir()
            (mutant / HEADER.name).write_text(text.replace(old, new))
            built, out = run(["g++", "-std=c++14", "-O1", "-I" + str(mutant), str(DRIVER), "-o", str(mutant / "driver")])
            if not built:
                check("mutant " + label + " is caught", False, out)
                continue
            caught, _ = run([str(mutant / "driver")], 2)
            check("mutant " + label + " is caught", caught)

    # The class: every option the game's own career class can set is one the visit keeps, and the visit keeps nothing the career lacks.
    setters = set(re.findall(r"\bvoid\s+Set(\w+)\s*\(", CAREER.read_text())) - NOT_OPTIONS
    kept = set(re.findall(r"career\.Set(\w+)\(", text))
    check("the visit keeps exactly the options the career can set (the setters of Career.h less progress)", setters == kept,
          "career only: %s; visit only: %s" % (sorted(setters - kept), sorted(kept - setters)))
    check("the visit reads each option it restores", {n for n in re.findall(r"career\.Get(\w+)\(", text)} == kept)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
