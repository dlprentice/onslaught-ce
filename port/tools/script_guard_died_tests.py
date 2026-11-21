#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What becomes of a script notification that patch 0176 defers: StartedDying runs a frame later, Died never runs.

    python3 -I port/tools/script_guard_died_tests.py [-v]

Patch 0176 makes IScript::StartedDying and IScript::Died, when they are entered while the script VM is running in a level a
mod supplies, schedule an event for the next frame and return. For StartedDying the event arrives and the handler runs then.
For Died it never does: every caller of IScript::Died deletes the script in its next statement, a deleted monitor clears
every reader that watches it, the scheduled event holds its target as such a reader, and the event manager skips an event
whose target is gone. So under the guard a nested Died handler is dropped, in every case, not delayed. This suite shows it
with the game's own text of each link of that chain, built on the host with port/tools/script_guard_died_driver.cpp:

- compiled whole from src/: SPtrSet, Monitor, activereader, event and scheduledevent;
- extracted whole from the patched MissionScript/IScript.cpp and IScript.h: both enumerations, the destructor, StartedDying,
  Died and HandleEvent;
- extracted whole from src/Unit.cpp: CUnit::Remove, one caller of Died; the other five are checked to have its three statements;
- port/platform/common/ce_script_guard.cpp itself.

The text is the default build's (the network's guarded lines resolved away, CE_VERSION defined).

Left out, with an authored stand-in in the driver or its prelude: the script VM (it records which handler was entered and
whether a handler was already running), the event manager (the suite checks that CEventManager::Flush tests an event's
target before each of its two handler calls, as the stand-in does), the game state, the mod list, the log, and the headers
named in LEFT_OUT (an empty file each). No script is compiled and no game runs: that a mod's level behaves so in the game is
not shown here.

Mutants: the Died hook taken out of the patch (the handler then runs nested, the crash the guard exists for), the caller's
delete taken out (the handler then does run a frame later: the delete is what drops it), and the monitor's destructor no
longer telling its readers (the event would then reach a deleted script). Each must change the output.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
sys.path.insert(0, str(PORT))
sys.path.insert(0, str(PORT / "tools"))
import patch_shape_tests as queue  # noqa: E402
import quiet_crash  # noqa: E402

CXX = "g++"
PATCH = "0176-ce-script-vm-guard.patch"
DRIVER = PORT / "tools/script_guard_died_driver.cpp"
PRELUDE = PORT / "tools/script_guard_died_prelude.h"
COMMON = PORT / "platform/common"
WHOLE = ("SPtrSet.h", "SPtrSet.cpp", "Listener.h", "Monitor.h", "Monitor.cpp", "activereader.h", "activereader.cpp", "Event.h", "event.cpp",
         "scheduledevent.h", "scheduledevent.cpp")
# Headers the whole files include that the prelude stands in for: an empty file each.
LEFT_OUT = ("common.h", "stdafx.h", "DebugLog.h", "profile.h")
CALLERS = ("BattleEngine.cpp", "Building.cpp", "HiveBoss.cpp", "Mine.cpp", "Unit.cpp", "thing.cpp")

LOG_LINE = "LOG script %s entered while the script VM was running (level 856 is a mod's): "


class Log:
    """LOG % entry: the guard's one log line for that entry (a Died is dropped; StartedDying waits a frame)."""
    def __mod__(self, entry):
        return LOG_LINE % entry + ("it is dropped (its script is deleted before the next frame)" if entry == "died" else "it waits for the next frame")


LOG = Log()
# scenario: (level, a mod supplies it, the driver's exact output)
CASES = {
    "a unit removed inside another script's handler, in a mod's level: its Died handler never runs": (856, 1, "removed-in-a-handler", [
        "FRAME 1",
        "HANDLER Init of the other script entered in frame 1",
        LOG % "died",
        "SCHEDULED event 2004 for the next frame",
        "DELETED the code of the victim's script",
        "SCHEDULED event 2000 for later",
        "the other script's handler returned",
        "FRAME 2",
        "SKIPPED event 2004: its target is gone",
        "FRAME 3",
        "DEFERRED 1"]),
    "a unit that starts dying inside another script's handler, in a mod's level: its StartedDying handler runs in the next frame": (856, 1, "starts-dying-in-a-handler", [
        "FRAME 1",
        "HANDLER Init of the other script entered in frame 1",
        LOG % "startedDying",
        "SCHEDULED event 2003 for the next frame",
        "the other script's handler returned",
        "FRAME 2",
        "HANDLER StartedDying of the victim's script entered in frame 2",
        "FRAME 3",
        "DEFERRED 1"]),
    "a unit that starts dying and is removed inside one handler, in a mod's level: neither handler runs": (856, 1, "starts-dying-and-removed-in-a-handler", [
        "FRAME 1",
        "HANDLER Init of the other script entered in frame 1",
        LOG % "startedDying",
        "SCHEDULED event 2003 for the next frame",
        LOG % "died",
        "SCHEDULED event 2004 for the next frame",
        "DELETED the code of the victim's script",
        "SCHEDULED event 2000 for later",
        "the other script's handler returned",
        "FRAME 2",
        "SKIPPED event 2003: its target is gone",
        "SKIPPED event 2004: its target is gone",
        "FRAME 3",
        "DEFERRED 2"]),
    "the same removal in a level the game ships: the handler runs nested, as the original runs it": (856, 0, "removed-in-a-handler", [
        "FRAME 1",
        "HANDLER Init of the other script entered in frame 1",
        "HANDLER Died of the victim's script entered in frame 1, NESTED in a running handler",
        "DELETED the code of the victim's script",
        "SCHEDULED event 2000 for later",
        "the other script's handler returned",
        "FRAME 2",
        "FRAME 3",
        "DEFERRED 0"]),
    "a unit removed while no handler runs, in a mod's level: the handler runs at once": (856, 1, "removed-outside-a-handler", [
        "FRAME 1",
        "HANDLER Died of the victim's script entered in frame 1",
        "DELETED the code of the victim's script",
        "SCHEDULED event 2000 for later",
        "FRAME 2",
        "FRAME 3",
        "DEFERRED 0"]),
}


def blank(source):
    """SOURCE with comments and string contents replaced by spaces, so braces can be counted (line breaks are kept)."""
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"', lambda m: re.sub(r"[^\n]", " ", m.group()), source, flags=re.S)


def definition(source, signature):
    """The whole text of the one definition whose head matches SIGNATURE (a regular expression): from its first character
    to its closing brace, with a `;` that follows (an enumeration's)."""
    clean = blank(source)
    found = [m for m in re.finditer(signature + r"[^;{}]*\{", clean)]
    if len(found) != 1:
        raise RuntimeError(f"{signature}: {len(found)} definitions found, 1 expected")
    start = found[0].start()
    at = found[0].end()
    depth = 1
    while depth:
        depth += (clean[at] == "{") - (clean[at] == "}")
        at += 1
    if clean[at:at + 1] == ";":
        at += 1
    return source[start:at]


def sources():
    """{name: text}: the mirror's form of the files this suite reads, IScript.* with the whole patch queue applied."""
    texts = {}
    form = queue.Sources()
    patches = [(p.name, p.read_text(encoding="latin-1")) for p in sorted(queue.PATCHES.glob("*.patch"))]
    for name in WHOLE + CALLERS + ("eventmanager.cpp", "eventmanager.h"):
        texts[name] = queue.by_program(form.text(ROOT / "src" / name, name), name, patches)
    for name in ("MissionScript/IScript.cpp", "MissionScript/IScript.h"):
        texts[name] = queue.by_program(form.text(ROOT / "src" / name, name), name, patches)
    # The default build's text: the network's guarded lines resolved away, as port/build.py gives them to the compiler.
    return {name: queue.build.network_view(text, off=True) for name, text in texts.items()}


def extracted(texts):
    """script_guard_died_extracted.inc: the enumerations, then the functions, each whole."""
    header, body = texts["MissionScript/IScript.h"], texts["MissionScript/IScript.cpp"]
    enumerations = [definition(header, r"\benum\s+EScriptEvent\b"), definition(header, r"\benum\s+EScriptMessage\b")]
    functions = [definition(body, r"\bIScript::~IScript\s*\("), definition(body, r"\bvoid\s+IScript::StartedDying\s*\("),
                 definition(body, r"\bvoid\s+IScript::Died\s*\("), definition(body, r"\bvoid\s+IScript::HandleEvent\s*\("),
                 definition(texts["Unit.cpp"], r"\bvoid\s+CUnit::Remove\s*\(")]
    # The sources keep the original's line numbers with #line; in an extract they would only mislead the compiler's messages.
    text = ("#ifdef SCRIPT_GUARD_DIED_ENUMERATIONS\n" + "\n\n".join(enumerations) + "\n#endif\n"
            "#ifdef SCRIPT_GUARD_DIED_FUNCTIONS\n" + "\n\n".join(functions) + "\n#endif\n")
    return re.sub(r"(?m)^[ \t]*#[ \t]*line\b[^\n]*\n", "\n", text)


def build(texts, folder, include=None):
    """The driver built in FOLDER from TEXTS; returns the program. Raises RuntimeError when it does not build."""
    folder = Path(folder)
    for name in WHOLE:
        (folder / name).write_text(texts[name], encoding="latin-1")
    for name in LEFT_OUT:
        (folder / name).write_text("")
    named = {m for name in WHOLE for m in re.findall(r'(?m)^\s*#\s*include\s*"([^"]+)"', texts[name])}
    missing = sorted(named - set(WHOLE) - set(LEFT_OUT))
    if missing:
        raise RuntimeError("a file compiled whole includes a header this suite neither copies nor lists as left out: " + ", ".join(missing))
    (folder / "script_guard_died_extracted.inc").write_text(include if include is not None else extracted(texts), encoding="latin-1")
    exe = folder / "driver"
    units = [str(DRIVER), str(COMMON / "ce_script_guard.cpp")] + [str(folder / name) for name in WHOLE if name.endswith(".cpp")]
    done = subprocess.run([CXX, "-std=gnu++17", "-O0", "-w", "-fpermissive", "-finput-charset=latin1", '-DCE_VERSION="test"', "-include", str(PRELUDE),
                           "-I", str(folder), "-I", str(COMMON),
                           *units, "-o", str(exe)], capture_output=True, text=True, errors="replace")
    if done.returncode:
        raise RuntimeError("the driver did not build:\n" + done.stderr[-3000:])
    return exe


def run(exe, level, supplied, scenario):
    # quiet_crash: one mutant makes the driver fault on purpose (an event delivered to a deleted script); it must leave no core dump
    done = quiet_crash.run([str(exe), str(level), str(supplied), scenario], capture_output=True, text=True, timeout=60)
    if done.returncode:
        raise RuntimeError(f"the driver ended with {done.returncode}:\n{done.stdout[-2000:]}{done.stderr[-2000:]}")
    return done.stdout.splitlines()


def disagreements(exe):
    out = []
    for name, (level, supplied, scenario, want) in CASES.items():
        try:
            got = run(exe, level, supplied, scenario)
        except (RuntimeError, subprocess.TimeoutExpired):
            got = None
        if got != want:
            out.append(name)
    return out


def caller_problems(texts):
    """What is wrong with the callers of IScript::Died: there must be six, each `Died(); delete the script; clear the pointer`."""
    problems, found = [], 0
    for name in sorted(p.name for p in (ROOT / "src").glob("*.cpp")):
        text = texts.get(name)
        if text is None:
            text = (ROOT / "src" / name).read_text(encoding="latin-1")
        clean = blank(text)
        for m in re.finditer(r"(\w+)\s*->\s*Died\s*\(\s*\)\s*;", clean):
            found += 1
            if name not in CALLERS:
                problems.append(f"{name}: a caller of Died this suite does not list")
            rest = clean[m.end():]
            if not re.match(rf"\s*delete\s+{m[1]}\s*;\s*{m[1]}\s*=\s*NULL\s*;", rest):
                problems.append(f"{name}: a caller of Died does not delete the script in its next statement")
    if found != len(CALLERS):
        problems.append(f"{found} callers of Died found, {len(CALLERS)} expected")
    return problems


def flush_problems(texts):
    """What is wrong with the event manager's side: the target is a reader, and Flush tests it before each handler call."""
    problems = []
    if not re.search(r"\bCActiveReader<CMonitor>\s+mToCall\s*;", blank(texts["Event.h"])):
        problems.append("Event.h: an event no longer holds its target as an active reader")
    if not re.search(r"(?m)^#define\s+NEXT_FRAME\s+-1\.0\s*$", texts["eventmanager.h"]):
        problems.append("eventmanager.h: NEXT_FRAME is not the driver's")
    flush = blank(definition(texts["eventmanager.cpp"], r"\bvoid\s+CEventManager::Flush\s*\("))
    calls = len(re.findall(r"->\s*HandleEvent\s*\(", flush))
    # the call sits inside the block of its target's test; other statements may share that block (patch 0280 brackets the call with
    # CE_ContactLeftoverDispatch), but no nested block, so the call cannot hide behind some other test
    tested = len(re.findall(r"IListener\s*\*\s*to_call\s*=\s*next_event\s*->\s*GetToCall\s*\(\s*\)\s*;[^{}]*?if\s*\(\s*to_call\s*\)\s*\{[^{}]*?\bto_call\s*->\s*HandleEvent\s*\(\s*next_event\s*\)\s*;[^{}]*?\}", flush))
    if calls != 2 or tested != 2:
        problems.append(f"CEventManager::Flush: {calls} handler calls, {tested} of them behind a test of the target; 2 and 2 expected")
    return problems


def wording_problems(patch):
    """What is wrong with what the patch says about a deferred Died."""
    problems = []
    died = re.search(r"\n \{\n\+#ifdef CE_VERSION\n\+\t// patch 0176: as StartedDying([^\n]*)\n(?:\+[^\n]*\n)*?\+\tif \(MISSION_SCRIPT_VM\.mRunning && PortCeScriptDeferNested\(GAME\.GetCurrentLevel\(\), \"died\"\)\)", patch)
    if not died:
        problems.append("the patch: the comment of the Died hook was not found")
    elif "dropped, not delayed" not in died[1] or "every caller deletes the script" not in died[1].lower() or "if the script is deleted" in died[1]:
        problems.append("the patch: the Died hook's comment must say the handler is dropped because every caller deletes the script")
    return problems


# name: (the file, text that occurs once in what the driver is built from, its replacement); a pair (A, B) in place of the text
# is the first B after A, which occurs once
MUTANTS = {
    "the Died hook is taken out": ("extract", 'PortCeScriptDeferNested(GAME.GetCurrentLevel(), "died")', "false"),
    "the caller does not delete the script": ("extract", "\t\tmMissionScript->Died();\n\t\tdelete mMissionScript;\n", "\t\tmMissionScript->Died();\n"),
    "a deleted monitor does not tell its readers": ("Monitor.cpp", ("CMonitor::~CMonitor()", "\tif (mDeletionList)"), "\tif (false)"),
    "the Died event is scheduled for the same frame's end and not the next frame": ("extract", "EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_DIED, this, NEXT_FRAME);", "EVENT_MANAGER.AddEvent(SCRIPT_MESSAGE_DEFERRED_STARTED_DYING, this, NEXT_FRAME);"),
}


class ScriptGuardDiedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.texts = sources()
        cls.folder = tempfile.TemporaryDirectory(prefix="script-guard-died-")
        cls.exe = build(cls.texts, cls.folder.name)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def test_a_deferred_died_is_dropped_and_a_deferred_started_dying_runs_a_frame_later(self):
        for name, (level, supplied, scenario, want) in CASES.items():
            with self.subTest(name):
                self.assertEqual(want, run(self.exe, level, supplied, scenario))

    def test_all_six_callers_of_died_delete_the_script_in_the_next_statement(self):
        self.assertEqual([], caller_problems(self.texts))
        mutant = dict(self.texts)
        mutant["Mine.cpp"] = mutant["Mine.cpp"].replace("\t\tdelete mMissionScript;\n", "", 1)
        self.assertNotEqual(self.texts["Mine.cpp"], mutant["Mine.cpp"])
        self.assertEqual(["Mine.cpp: a caller of Died does not delete the script in its next statement"], caller_problems(mutant))
        mutant = dict(self.texts)
        mutant["world.cpp"] = "void f()\n{\n\tmScript->Died();\n\tdelete mScript;\n\tmScript = NULL;\n}\n"
        self.assertEqual(["world.cpp: a caller of Died this suite does not list", "7 callers of Died found, 6 expected"], caller_problems(mutant))

    def test_the_event_manager_skips_an_event_whose_target_is_gone(self):
        self.assertEqual([], flush_problems(self.texts))
        mutant = dict(self.texts)
        # the second call loses its target test: the block's statements stay, unguarded (whatever else shares the block)
        mutant["eventmanager.cpp"] = re.sub(r"\t\tif \(to_call\)\n\t\t\{\n([^{}\n]*\bto_call->HandleEvent\(next_event\);[^{}\n]*\n)\t\t\}\n",
                                            r"\1", mutant["eventmanager.cpp"], count=1)
        self.assertNotEqual(self.texts["eventmanager.cpp"], mutant["eventmanager.cpp"])
        self.assertEqual(1, len(flush_problems(mutant)))

    def test_the_patch_says_that_a_deferred_died_is_dropped(self):
        patch = (queue.PATCHES / PATCH).read_text(encoding="latin-1")
        self.assertEqual([], wording_problems(patch))
        old = "+\t// patch 0176: as StartedDying; if the script is deleted before the next frame the event is dropped (by code reading)\n"
        new = next(line + "\n" for line in patch.split("\n") if line.startswith("+\t// patch 0176: as StartedDying"))
        self.assertEqual(1, len(wording_problems(patch.replace(new, old))), "the comment as it was must be refused")

    def test_each_mutant_changes_the_output(self):
        survivors = []
        include = extracted(self.texts)
        for name, (where, old, new) in MUTANTS.items():
            texts, text = dict(self.texts), include
            if where == "extract":
                self.assertEqual(1, text.count(old), f"{name}: the anchor must occur once")
                text = text.replace(old, new)
            elif isinstance(old, tuple):
                anchor, old = old
                self.assertEqual(1, texts[where].count(anchor), f"{name}: the anchor must occur once")
                at = texts[where].index(old, texts[where].index(anchor))
                texts[where] = texts[where][:at] + new + texts[where][at + len(old):]
            else:
                self.assertEqual(1, texts[where].count(old), f"{name}: the anchor must occur once")
                texts[where] = texts[where].replace(old, new)
            with tempfile.TemporaryDirectory(prefix="script-guard-died-mutant-") as folder:
                exe = build(texts, folder, text)
                if not disagreements(exe):
                    survivors.append(name)
        self.assertEqual([], survivors)

    def test_the_extract_keeps_each_function_whole(self):
        text = extracted(self.texts)
        body = self.texts["MissionScript/IScript.cpp"]
        for head in ("IScript::~IScript()", "IScript::StartedDying()", "IScript::Died()", "IScript::HandleEvent(CEvent *event)"):
            self.assertEqual(1, text.count(head), head)
        # HandleEvent to its last case and the closing braces of its switch and body, as the patched file has them.
        tail = body[body.index("IScript::HandleEvent(CEvent *event)"):]
        tail = tail[:tail.index("\n}\n") + 3]
        self.assertIn(re.sub(r"(?m)^[ \t]*#[ \t]*line\b[^\n]*\n", "\n", tail), text)
        self.assertIn("case SCRIPT_MESSAGE_DEFERRED_DIED:", tail)
        self.assertIn("case SCRIPT_MESSAGE_FOLLOW_WAYPOINT:", tail)
        self.assertEqual(blank(text).count("{"), blank(text).count("}"))


if __name__ == "__main__":
    unittest.main(verbosity=2 if "-v" in sys.argv else 1, argv=[a for a in sys.argv if a != "-v"])
