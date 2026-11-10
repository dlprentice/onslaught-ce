#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The script VM guard of patch 0176 (port/platform/common/ce_script_guard.cpp and the two IScript hooks it adds).

    python3 port/tools/script_guard_tests.py

The guard decides, for a script notification (`startedDying` or `died`) that is entered while the mission script VM is still running, whether it waits a frame: yes only in a
level an active mod supplies (the level's archive, data/resources/NNN_res_PC.aya, or its world file, data/worlds/worldNNN.wrd), no for every level the game ships, and it counts
and logs each entry once. This suite builds the actual ce_script_guard.cpp with the driver port/tools/tests/ce_script_guard_driver.cpp on the host (g++), whose two fakes are
PortModHas (yes for the game paths named before `--`) and PortSessionLog (prints what it is given), and asks it the questions below; it then checks the text of the patch
(which files, the four CE_VERSION blocks, the two hooks and their deferred events) and runs mutants of both: each must change an answer, a log line, a count or a patch check.
It does not run a game: that the hooks do the right thing inside the VM is what the build, the tapes and a level supplied by a mod must show.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "port/platform/common"
GUARD = COMMON / "ce_script_guard.cpp"
DRIVER = ROOT / "port/tools/tests/ce_script_guard_driver.cpp"
PATCH = ROOT / "port/patches/0176-ce-script-vm-guard.patch"
SRC = ROOT / "src/MissionScript"
MINGW = "llvm-mingw/bin/i686-w64-mingw32-clang++"               # under BEA_PORT_DATA (port/tools/deps.py): the toolchain the Windows build uses
CXX = os.environ.get("CXX", "g++")

LOG = "LOG 0 1 script %s entered while the script VM was running (level %d is a mod's): it waits for the next frame"

# name: (game paths a mod supplies, questions "LEVEL:ENTRY", the driver's exact output)
CASES = {
    "a level supplied by its archive defers both notifications, logs each entry once and counts every yes": (
        ["data/resources/856_res_PC.aya"], ["856:startedDying", "856:died", "100:died", "856:startedDying"],
        [LOG % ("startedDying", 856), "ASK 856 startedDying yes", LOG % ("died", 856), "ASK 856 died yes", "ASK 100 died no", "ASK 856 startedDying yes", "COUNT 3"]),
    "a level supplied by its world file alone defers too": (
        ["data/worlds/world857.wrd"], ["857:died", "856:died"],
        [LOG % ("died", 857), "ASK 857 died yes", "ASK 856 died no", "COUNT 1"]),
    "a level nothing supplies waits for nothing": (
        [], ["856:died", "100:startedDying", "0:died"],
        ["ASK 856 died no", "ASK 100 startedDying no", "ASK 0 died no", "COUNT 0"]),
    "the level number is three digits in both paths": (
        ["data/resources/005_res_PC.aya", "data/worlds/world012.wrd"], ["5:died", "12:died", "50:died", "6:died"],
        [LOG % ("died", 5), "ASK 5 died yes", "ASK 12 died yes", "ASK 50 died no", "ASK 6 died no", "COUNT 2"]),
    "the archive and the world file of one level are one yes each time": (
        ["data/resources/860_res_PC.aya", "data/worlds/world860.wrd"], ["860:startedDying", "860:startedDying"],
        [LOG % ("startedDying", 860), "ASK 860 startedDying yes", "ASK 860 startedDying yes", "COUNT 2"]),
    "a path of another kind or another level is not a supply": (
        ["data/resources/856_res_PC.aya", "data/resources/857_res_PS2.aya", "data/worlds/world858.wrd.bak", "data/textures/860.aya"], ["857:died", "858:died", "860:died", "856:died"],
        ["ASK 857 died no", "ASK 858 died no", "ASK 860 died no", LOG % ("died", 856), "ASK 856 died yes", "COUNT 1"]),
    "any entry but died shares the first notification's single log line": (
        ["data/resources/856_res_PC.aya"], ["856:other", "856:startedDying", "856:died"],
        [LOG % ("other", 856), "ASK 856 other yes", "ASK 856 startedDying yes", LOG % ("died", 856), "ASK 856 died yes", "COUNT 3"]),
}

# name: (text in ce_script_guard.cpp that occurs once, its replacement)
GUARD_MUTANTS = {
    "the archive path loses its three-digit level": ("data/resources/%03d_res_PC.aya", "data/resources/%d_res_PC.aya"),
    "the archive path names another folder": ("data/resources/%03d_res_PC.aya", "data/resource/%03d_res_PC.aya"),
    "the archive path names another extension": ("_res_PC.aya", "_res_PC.ayb"),
    "the world path loses its three-digit level": ("data/worlds/world%03d.wrd", "data/worlds/world%d.wrd"),
    "the world path names another extension": ("world%03d.wrd", "world%03d.wrx"),
    "the world file is not asked for": ("\tif (!supplied)\n\t{\n\t\tsnprintf(path, sizeof(path), \"data/worlds/world%03d.wrd\", level);\n\t\tsupplied = PortModHas(path);\n\t}\n", ""),
    "a level nobody supplies is deferred": ("\tif (!supplied)\n\t\treturn false;", "\tif (!supplied)\n\t\treturn true;"),
    "a supplied level is not deferred": ("\treturn true;\n}\n\nint PortCeScriptDeferred", "\treturn false;\n}\n\nint PortCeScriptDeferred"),
    "a yes is not counted": ("\tsDeferred++;\n", "\t;\n"),
    "a yes is counted twice": ("\tsDeferred++;\n", "\tsDeferred += 2;\n"),
    "the count reported is off by one": ("\treturn sDeferred;", "\treturn sDeferred + 1;"),
    "the entries share one log flag": ("int which = strcmp(entry, \"died\") == 0 ? 1 : 0;", "int which = 0;"),
    "a yes is logged every time": ("\t\tsLogged[which] = true;\n", "\t\t;\n"),
    "a yes is never logged": ("\tif (!sLogged[which])", "\tif (false)"),
    "the log line is not an error": ("PortSessionLog(PORT_SESSION_GAME, true,", "PortSessionLog(PORT_SESSION_GAME, false,"),
    "the log line names no level": ("(level %d is a mod's)", "(level is a mod's)"),
}


def build(source_text: str, folder: str) -> Path:
    guard = Path(folder) / "ce_script_guard.cpp"
    guard.write_text(source_text)
    exe = Path(folder) / "driver"
    done = subprocess.run([CXX, "-std=gnu++17", "-O0", "-w", "-I", str(COMMON), str(DRIVER), str(guard), "-o", str(exe)], capture_output=True, text=True)
    if done.returncode:
        raise RuntimeError("the guard did not build:\n" + done.stderr[-1500:])
    return exe


def ask(exe: Path, supplied: list, questions: list) -> list:
    done = subprocess.run([str(exe), *supplied, "--", *questions], capture_output=True, text=True, check=True)
    return done.stdout.splitlines()


def disagreements(exe: Path) -> list:
    return [name for name, (supplied, questions, want) in CASES.items() if ask(exe, supplied, questions) != want]


def strip_ce_blocks(text: str) -> str:
    """The text as a build that does not define CE_VERSION reads it: every `#ifdef CE_VERSION` .. `#endif` block (none nests) removed."""
    return re.sub(r"[ \t]*#ifdef CE_VERSION\n.*?[ \t]*#endif\n", "", text, flags=re.S)


def patch_problems(text: str) -> list:
    """What is wrong with the text of patch 0176: the files it touches, the four CE_VERSION blocks and what is inside them."""
    problems = []
    if re.findall(r"^\+\+\+ b/(\S+)$", text, re.M) != ["MissionScript/IScript.cpp", "MissionScript/IScript.h"]:
        problems.append("the patch must touch exactly MissionScript/IScript.cpp and MissionScript/IScript.h")
    if text.count("\n+#ifdef CE_VERSION\n") != 4 or text.count("\n+#endif\n") != 4:
        problems.append("the patch must add four #ifdef CE_VERSION blocks (two hooks, the event cases, the enumerators) and four #endif")
    for entry, event in (("startedDying", "SCRIPT_MESSAGE_DEFERRED_STARTED_DYING"), ("died", "SCRIPT_MESSAGE_DEFERRED_DIED")):
        hook = (f'\n+\tif (MISSION_SCRIPT_VM.mRunning && PortCeScriptDeferNested(GAME.GetCurrentLevel(), "{entry}"))\n'
                f'+\t{{\n+\t\tEVENT_MANAGER.AddEvent({event}, this, NEXT_FRAME);\n+\t\treturn;\n+\t}}\n')
        if text.count(hook) != 1:
            problems.append(f"the {entry} hook must test the VM is running, ask the guard about the current level, and defer to the next frame by {event}")
    for event, call in (("SCRIPT_MESSAGE_DEFERRED_STARTED_DYING", "StartedDying();"), ("SCRIPT_MESSAGE_DEFERRED_DIED", "Died();")):
        if not re.search(rf"\n\+\tcase {event}:\n\+\t\t{re.escape(call)}[^\n]*\n\+\t\tbreak;\n", text):
            problems.append(f"HandleEvent must run {call} on {event} and break")
    if not re.search(r"SCRIPT_MESSAGE_TIMER\s+// SetTimer's timer went off\n\+#ifdef CE_VERSION\n\+\t,\n\+\tSCRIPT_MESSAGE_DEFERRED_STARTED_DYING,[^\n]*\n\+\tSCRIPT_MESSAGE_DEFERRED_DIED\s[^\n]*\n\+#endif\n", text):
        problems.append("the two enumerators must follow SCRIPT_MESSAGE_TIMER inside a CE_VERSION block")
    if text.count("extern bool PortCeScriptDeferNested(int level, const char *entry);") != 2:
        problems.append("each hook declares PortCeScriptDeferNested")
    return problems


PATCH_MUTANTS = {
    "the died hook asks about the other entry": ('PortCeScriptDeferNested(GAME.GetCurrentLevel(), "died")', 'PortCeScriptDeferNested(GAME.GetCurrentLevel(), "startedDying")'),
    "the startedDying hook does not test the VM is running": ("if (MISSION_SCRIPT_VM.mRunning && PortCeScriptDeferNested(GAME.GetCurrentLevel(), \"startedDying\"))", "if (PortCeScriptDeferNested(GAME.GetCurrentLevel(), \"startedDying\"))"),
    "a hook asks about level zero": ("PortCeScriptDeferNested(GAME.GetCurrentLevel(), \"startedDying\")", "PortCeScriptDeferNested(0, \"startedDying\")"),
    "a hook runs now instead of next frame": ("EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, NEXT_FRAME);", "EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, 0);"),
    "a hook defers by the other event": ("EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_STARTED_DYING, this, NEXT_FRAME);", "EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, NEXT_FRAME);"),
    "a hook does not return after deferring": ("\t\tEVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, NEXT_FRAME);\n+\t\treturn;\n", "\t\tEVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, NEXT_FRAME);\n"),
    "HandleEvent runs Died on the startedDying event": ("case SCRIPT_MESSAGE_DEFERRED_STARTED_DYING:\n+\t\tStartedDying();", "case SCRIPT_MESSAGE_DEFERRED_STARTED_DYING:\n+\t\tDied();"),
    "HandleEvent has no case for the died event": ("\n+\tcase SCRIPT_MESSAGE_DEFERRED_DIED:\n+\t\tDied();\n+\t\tbreak;\n", "\n"),
    "a block loses its CE_VERSION guard": ("+#ifdef CE_VERSION\n+\t// patch 0176: as StartedDying", "+\t// patch 0176: as StartedDying"),
    "the patch touches a third file": ("--- a/MissionScript/IScript.h\n+++ b/MissionScript/IScript.h", "--- a/MissionScript/IScript.h\n+++ b/MissionScript/IScript2.h"),
    "a hook loses its declaration": ("+\textern bool PortCeScriptDeferNested(int level, const char *entry);\n+\tif (MISSION_SCRIPT_VM.mRunning && PortCeScriptDeferNested(GAME.GetCurrentLevel(), \"died\"))", "+\tif (MISSION_SCRIPT_VM.mRunning && PortCeScriptDeferNested(GAME.GetCurrentLevel(), \"died\"))"),
}


class ScriptGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = GUARD.read_text()
        cls.patch = PATCH.read_text()
        cls.folder = tempfile.TemporaryDirectory(prefix="script-guard-")
        cls.exe = build(cls.source, cls.folder.name)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def test_the_questions_get_the_answers_the_log_lines_and_the_counts_of_the_design(self):
        for name, (supplied, questions, want) in CASES.items():
            with self.subTest(name):
                self.assertEqual(want, ask(self.exe, supplied, questions))

    def test_the_patch_adds_the_two_hooks_the_event_cases_and_the_enumerators_and_nothing_else(self):
        self.assertEqual([], patch_problems(self.patch))

    def test_the_guard_calls_only_the_two_things_the_driver_fakes(self):
        calls = set(re.findall(r"\b(Port[A-Za-z]+)\s*\(", self.source))
        self.assertEqual({"PortModHas", "PortSessionLog", "PortCeScriptDeferNested", "PortCeScriptDeferred"}, calls)

    def test_the_patch_applies_alone_to_the_raw_source_without_fuzz_and_a_build_without_CE_VERSION_reads_the_raw_text(self):
        if shutil.which("patch") is None:
            self.fail("patch(1) is needed to try the patch on the raw source; it is not on the PATH")
        with tempfile.TemporaryDirectory(prefix="script-guard-patch-") as folder:
            tree = Path(folder) / "MissionScript"
            tree.mkdir()
            for name in ("IScript.cpp", "IScript.h"):
                shutil.copyfile(SRC / name, tree / name)
            done = subprocess.run(["patch", "-p1", "--fuzz=0", "--no-backup-if-mismatch", "-d", folder, "-i", str(PATCH)], capture_output=True, text=True)
            self.assertEqual(0, done.returncode, done.stdout + done.stderr)
            self.assertNotIn("fuzz", done.stdout.lower())
            for name in ("IScript.cpp", "IScript.h"):
                patched = (tree / name).read_text(encoding="latin-1")
                raw = (SRC / name).read_text(encoding="latin-1")
                self.assertNotEqual(raw, patched, name)
                self.assertIn("patch 0176", patched, name)
                self.assertEqual(raw, strip_ce_blocks(patched), f"{name}: outside the CE_VERSION blocks the patched text is the raw text")

    def test_the_guard_compiles_for_windows_with_the_toolchain_of_the_windows_build(self):
        data = os.environ.get("BEA_PORT_DATA")
        compiler = Path(data) / MINGW if data else None
        if compiler is None or not compiler.is_file():
            self.skipTest("SKIPPED, NOT PASSED: no Windows toolchain (set BEA_PORT_DATA to the dependency data holding " + MINGW + ")")
        with tempfile.TemporaryDirectory(prefix="script-guard-win-") as folder:
            done = subprocess.run([str(compiler), "-std=gnu++17", "-fshort-wchar", "-mms-bitfields", "-DNOMINMAX", "-D_WIN32_WINNT=0x0a00", "-Wall", "-Wextra", "-I", str(COMMON),
                                   "-c", str(GUARD), "-o", str(Path(folder) / "ce_script_guard.o")], capture_output=True, text=True)
            self.assertEqual(0, done.returncode, done.stderr[-1500:])
            self.assertEqual("", done.stderr.strip(), "no warning under -Wall -Wextra")

    def test_each_mutant_of_the_guard_changes_an_answer_a_log_line_or_a_count(self):
        survivors = []
        for name, (old, new) in GUARD_MUTANTS.items():
            self.assertEqual(1, self.source.count(old), f"{name}: the anchor must occur once")
            with tempfile.TemporaryDirectory(prefix="script-guard-mutant-") as folder:
                try:
                    exe = build(self.source.replace(old, new), folder)
                except RuntimeError:
                    continue                                    # a mutant that does not build is killed by the compiler
                if not disagreements(exe):
                    survivors.append(name)
        self.assertEqual([], survivors)

    def test_each_mutant_of_the_patch_is_found_by_the_text_checks(self):
        survivors = []
        for name, (old, new) in PATCH_MUTANTS.items():
            self.assertEqual(1, self.patch.count(old), f"{name}: the anchor must occur once")
            if not patch_problems(self.patch.replace(old, new)):
                survivors.append(name)
        self.assertEqual([], survivors)


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[a for a in sys.argv if a != "-v"])
