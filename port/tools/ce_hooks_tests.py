#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The CE hook points (port/game/ce_hooks.h): one compiled, ordered table, and one call per site.

    python3 port/tools/ce_hooks_tests.py [-v]

Rules, each with mutants that must be caught:
  1. slots: within one point's table no two handlers claim the same slot, and the slots ascend (the order they run in);
  2. tags: a handler's SIM/RENDER tag equals its point's, both ways (a RENDER handler on a SIM point would run presentation
     code inside the simulation's step; a SIM handler on a RENDER point would change the simulation at a frame rate's
     pace); audio_clock handlers take CE_GATE_VERSION and CE_MODE_NONE only (they may run on the music worker thread);
  3. sites: every point is called exactly where SITES says (file, function, number of calls) in the mirror's text with
     every patch applied, and nowhere else; each site patch is in the queue;
  4. the table compiles: ce_hooks.cpp built on the host with CE_VERSION refuses each table mutant (its static_asserts),
     and with no handler every point returns at once (a key is never consumed, a camera and a Battle Engine come back
     unchanged);
  5. the mode registry (ce_modes.cpp): begin, end, refusal on conflict, the one compatible pair;
  6. dispatch: each point names its dispatch (DISPATCH), and each dispatch does what ce_hooks_dispatch.h says, on host
     tables of its own (broadcast order, first match, filter chaining, override chaining, positions, the command queue,
     gates and modes, and the test builds' guard around every handler);
  7. site patches rest on the game's own lines: each 03xx patch still applies with any one other patch of its files left
     out (DEPENDS names the one exception: the finished poll is patch 0062's own function).
The save filters' order hazard (CCareer::Save runs the editor's, the bonus level's and the museum visit's filters, each
replacing the whole career with its own snapshot, so with two active the later one wins) is shown and kept as an
EXPECTED FAILURE: it is a later change (one owner of the career snapshot), not part of the hook points.
No game data, device or network is used; scratch goes to local-data/port/ce-hooks-tests.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT))
import build  # noqa: E402

HOOKS = PORT / "game/ce_hooks.cpp"
HEADER = PORT / "game/ce_hooks.h"
MODES = PORT / "game/ce_modes.cpp"
SCRATCH = ROOT / "local-data/port/ce-hooks-tests"

POINTS = {  # point: tag (the header's table)
    "boot": "SIM", "exit": "SIM", "level_begin": "SIM", "level_end": "SIM", "tick_pre": "SIM", "tick_post": "SIM",
    "input_actions": "SIM", "key_capture": "RENDER", "draw_world": "RENDER", "draw_hud": "RENDER", "draw_overlay": "RENDER",
    "menu_rows": "RENDER", "save_gate": "SIM", "audio_clock": "RENDER", "render_camera": "SIM", "hud_focus": "RENDER"}
# point: (patch, [(file, function, calls)])
SITES = {
    "boot": ("0301-ce-hook-boot", [("ltshell.cpp", "WinMain", 1)]),
    "exit": ("0302-ce-hook-exit", [("ltshell.cpp", "ExitCode", 1), ("ltshell.cpp", "PCLTShell::MainLoop", 1)]),
    "level_begin": ("0303-ce-hook-level-begin", [("game.cpp", "CGame::RestartLoopRunLevel", 1)]),
    "level_end": ("0304-ce-hook-level-end", [("game.cpp", "CGame::ShutdownRestartLoop", 1)]),
    "tick_pre": ("0305-ce-hook-tick-pre", [("game.cpp", "CGame::Update", 1)]),
    "tick_post": ("0306-ce-hook-tick-post", [("game.cpp", "CGame::Update", 1)]),
    "input_actions": ("0307-ce-hook-input-actions", [("Controller.cpp", "CController::DoMappings", 1)]),
    "key_capture": ("0308-ce-hook-key-capture", [("ltshell.cpp", "PCLTShell::MsgProc", 1)]),
    "draw_world": ("0309-ce-hook-draw-world", [("DXEngine.cpp", "draw_fairly_late_stuff", 1)]),
    "draw_hud": ("0310-ce-hook-draw-hud", [("Hud.cpp", "CHud::RenderOverlayForViewpoint", 1)]),
    "draw_overlay": ("0311-ce-hook-draw-overlay", [("DXEngine.cpp", "CDXEngine::PostRender", 1)]),
    "menu_rows": ("0312-ce-hook-menu-rows", [("FrontEnd.cpp", "CFrontEnd::Init", 1), ("PauseMenu.cpp", "CPauseMenu::CPauseMenu", 2)]),
    "save_gate": ("0313-ce-hook-save-gate", [("Career.cpp", "CCareer::Save", 1), ("FEPOptions.cpp", "CFEPOptions::WriteDefaultOptionsFile", 1)]),
    "audio_clock": ("0314-ce-hook-audio-clock", [("PCMusicStream.cpp", "GetMusicStreamFinished", 1), ("PCMusicStream.cpp", "MusicStreamHandle", 1)]),
    "render_camera": ("0315-ce-hook-render-camera", [("engine.cpp", "CEngine::SetViewpoint", 1)]),
    "hud_focus": (None, []),
}
DISPATCH = {
    "boot": "ORDERED_BROADCAST", "exit": "ORDERED_BROADCAST", "level_begin": "ORDERED_BROADCAST", "level_end": "ORDERED_BROADCAST",
    "tick_pre": "ORDERED_BROADCAST", "tick_post": "COMMAND_APPLY", "input_actions": "ORDERED_BROADCAST", "key_capture": "FIRST_MATCH",
    "draw_world": "ORDERED_BROADCAST", "draw_hud": "ORDERED_BROADCAST", "draw_overlay": "ORDERED_BROADCAST",
    "menu_rows": "POSITIONAL_INSERT", "save_gate": "FILTER", "audio_clock": "ORDERED_BROADCAST", "render_camera": "OVERRIDE_CHAIN",
    "hud_focus": "OVERRIDE_CHAIN"}
DEPENDS = {("0314-ce-hook-audio-clock", "0062-music-finished-poll"), ("0313-ce-hook-save-gate", "0052-ce-god-not-saved-Career")}
SITE_FILES = sorted({f for _, sites in SITES.values() for f, _, _ in sites})
TABLE = re.compile(r"static constexpr CeHookRow<\w+> (k\w+)\[\] = \{(.*?)\n\s*CE_HOOK_END\n\};\nCE_HOOK_CHECK\(\1, CE_HOOK_(SIM|RENDER), CE_DISPATCH_(\w+), \"(\w+)\"\);", re.S)
ROW = re.compile(r"\{\s*(-?\d+)\s*,\s*\"([^\"]+)\"\s*,\s*CE_HOOK_(\w+)\s*,\s*CE_GATE_(\w+)\s*,\s*CE_MODE_(\w+)\s*,\s*(\w+)\s*\}")


def table(text):
    """{point: (tag, dispatch, [(slot, name, tag, gate, mode, fn)])} of ce_hooks.cpp's text."""
    out = {}
    for _, body, tag, dispatch, point in TABLE.findall(text):
        body = "\n".join(line.split("//", 1)[0] for line in body.splitlines())
        out[point] = (tag, dispatch, [(int(m[0]), m[1], m[2], m[3], m[4], m[5]) for m in ROW.findall(body)])
    return out


def table_problems(text):
    """The rule 1 and 2 problems of a table's text, one line each."""
    problems = []
    rows = table(text)
    if set(rows) != set(POINTS):
        problems.append(f"points: table has {sorted(rows)}, header has {sorted(POINTS)}")
    for point, (tag, dispatch, handlers) in rows.items():
        if POINTS.get(point) != tag:
            problems.append(f"{point}: table tags the point {tag}, the header {POINTS.get(point)}")
        if DISPATCH.get(point) != dispatch:
            problems.append(f"{point}: dispatch {dispatch}, the design says {DISPATCH.get(point)}")
        slots = [h[0] for h in handlers]
        if len(set(slots)) != len(slots):
            problems.append(f"{point}: two handlers claim one slot ({slots})")
        elif slots != sorted(slots):
            problems.append(f"{point}: slots out of order ({slots})")
        for slot, name, htag, gate, mode, _ in handlers:
            if htag != tag:
                problems.append(f"{point}: handler {name} is {htag} on a {tag} point")
            if point == "audio_clock" and (gate != "VERSION" or mode != "NONE"):
                problems.append(f"audio_clock: handler {name} needs CE_GATE_VERSION and CE_MODE_NONE")
    return problems


HEADER_NAME = re.compile(r"((?:\w+::)*~?\w+)\s*$")


def enclosing_function(lines, index):
    """The name of the function whose definition encloses line `index`: the nearest line above it that starts at column 0
    with code holding a '(' (a definition's header; the game's bodies are indented, and its declarations end in ';')."""
    for i in range(index, -1, -1):
        line = lines[i].split("//", 1)[0].rstrip()
        if not line or line[0] in " \t#{}/*" or "(" not in line or line.endswith(";"):
            continue
        m = HEADER_NAME.search(line[:line.index("(")])
        if m:
            return m[1]
    return None


def site_problems(texts, queue):
    """Rule 3 problems for {file: fully patched text} and the patch names in the queue."""
    problems = []
    for point, (patch, sites) in SITES.items():
        if patch is not None and patch not in queue:
            problems.append(f"{point}: its site patch {patch} is missing from the queue")
        call = re.compile(r"\bce_hook_" + point + r"\s*\(")
        want = {(f, fn): n for f, fn, n in sites}
        found = {}
        for name, text in texts.items():
            lines = text.split("\n")
            for i, line in enumerate(lines):
                code = line.split("//", 1)[0]
                if call.search(code):
                    key = (name, enclosing_function(lines, i))
                    found[key] = found.get(key, 0) + len(call.findall(code))
        for key, n in want.items():
            if found.get(key, 0) != n:
                problems.append(f"{point}: {key[0]} {key[1]} has {found.get(key, 0)} calls, {n} expected")
        for key, n in found.items():
            if key not in want:
                problems.append(f"{point}: an unexpected call in {key[0]} {key[1]} ({n})")
    return problems


def compile_host(out, sources, defines=("-DCE_VERSION=\"hooks-test\"",), driver=None):
    """Compiles sources (+ driver text) on the host; (returncode, output)."""
    out.mkdir(parents=True, exist_ok=True)
    files = list(sources)
    if driver is not None:
        (out / "driver.cpp").write_text(driver)
        files.append(out / "driver.cpp")
    command = [shutil.which("g++") or "c++", "-std=gnu++14", "-Wall", "-Werror", "-Wno-unused-function", *defines,
               "-I", str(PORT / "game"), *map(str, files), "-o", str(out / "driver")]
    if driver is None:
        command[-2:] = []
        command += ["-c", "-o", str(out / "object.o")] if len(files) == 1 else []
    result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


STUBS = "bool PortCeLayer() { return true; }\n"
MUTANT_FN = "static void CeHookMutant() {}\nstatic void CeHookMutant2() {}\nstatic void CeHookMutantAudio(CeHookAudioEvent, unsigned) {}\nstatic void CeHookMutantCommand(const CeHookCommand *) {}\n"


def mutate_table(text, point, rows):
    """ce_hooks.cpp's text with `rows` (C++ row text) put into `point`'s table."""
    m = [m for m in TABLE.finditer(text) if m[5] == point][0]
    start = m.start(2) + len(m[2])
    return text[:start] + "\n" + rows + text[start:]


TABLE_MUTANTS = {  # name: (point, rows, the problem text expected)
    "two handlers in one slot": ("tick_pre", '\t{90, "a", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant},\n'
                                             '\t{90, "b", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant2},', "two handlers claim one slot"),
    "slots out of order": ("tick_pre", '\t{95, "a", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant},\n'
                                       '\t{90, "b", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant2},', "out of order"),
    "a RENDER handler on a SIM point": ("tick_pre", '\t{90, "a", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant},', "is RENDER on a SIM point"),
    "a SIM handler on a RENDER point": ("draw_overlay", '\t{90, "a", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookMutant},', "is SIM on a RENDER point"),
}


class TableTests(unittest.TestCase):
    def setUp(self):
        self.text = HOOKS.read_text()

    def test_every_point_is_declared_tabled_and_defined_in_both_branches(self):
        header = HEADER.read_text()
        for point in POINTS:
            self.assertEqual(len(re.findall(r"\bce_hook_" + point + r"\(", header)), 1, point)
            self.assertEqual(len(re.findall(r"^\S.*\bce_hook_" + point + r"\(", self.text, re.M)), 2, point + ": a CE and a private definition")
        self.assertEqual(len(re.findall(r"^bool ce_hook_command_push\(", self.text, re.M)), 2)
        self.assertEqual(table_problems(self.text), [])

    def test_the_rules_catch_each_table_mutant(self):
        for name, (point, rows, expected) in TABLE_MUTANTS.items():
            with self.subTest(mutant=name):
                problems = table_problems(mutate_table(self.text, point, rows))
                self.assertTrue(any(expected in p for p in problems), problems)
        worker = mutate_table(self.text.replace('static_assert(CeHookWorkerSafe', '// static_assert(CeHookWorkerSafe'), "audio_clock",
                              '\t{10, "a", CE_HOOK_RENDER, CE_GATE_LAYER, CE_MODE_NONE, CeHookMutant},')
        self.assertTrue(any("needs CE_GATE_VERSION" in p for p in table_problems(worker)))
        swapped = self.text.replace('CE_HOOK_CHECK(kKeyCapture, CE_HOOK_RENDER, CE_DISPATCH_FIRST_MATCH', 'CE_HOOK_CHECK(kKeyCapture, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST')
        self.assertTrue(any("key_capture: dispatch ORDERED_BROADCAST" in p for p in table_problems(swapped)))

    def test_the_table_compiles_and_its_static_asserts_catch_each_mutant(self):
        base = Path(tempfile.mkdtemp(prefix="table-", dir=SCRATCH))
        try:
            def build_text(text, name):
                path = base / name / "ce_hooks.cpp"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text.replace('#include "ce_hooks_dispatch.h"\n', '#include "ce_hooks_dispatch.h"\n' + MUTANT_FN))
                return compile_host(path.parent, [path])
            rc, log = build_text(self.text, "clean")
            self.assertEqual(rc, 0, log)
            for name, (point, rows, _) in TABLE_MUTANTS.items():
                with self.subTest(mutant=name):
                    rc, log = build_text(mutate_table(self.text, point, rows), name.replace(" ", "-"))
                    self.assertNotEqual(rc, 0, f"{name} compiled")
                    self.assertIn("static assert", log.replace("static_assert", "static assert"))
            rc, log = build_text(mutate_table(self.text, "audio_clock", '\t{10, "a", CE_HOOK_RENDER, CE_GATE_LAYER, CE_MODE_NONE, CeHookMutantAudio},'), "worker")
            self.assertIn("music worker thread", log)
            self.assertNotEqual(rc, 0, "an audio_clock handler behind PortCeLayer compiled")
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_with_no_handler_every_point_is_inert(self):
        driver = STUBS + r'''
#include "ce_hooks.h"
#include "ce_hooks_dispatch.h"
#include <cstdio>
#include <string>
#ifdef CE_VERSION
static std::string gCalls;
static unsigned gConsumeKey = 0;
void PortCeGamepadBeginCycle() { gCalls += "gamepad;"; }
void PortCePaletteClose() { gCalls += "palette_end;"; }
void PortCeConsoleLevelEnd() { gCalls += "console_end;"; }
void PortCePhotoLevelEnd() { gCalls += "photo_end;"; }
void PortCeCmdLevelEnd() { gCalls += "cmd_end;"; }
bool PortCePhotoKey(unsigned key, bool) { gCalls += "photo_key;"; return key == gConsumeKey && key == 1; }
bool PortCePaletteSuppressDown(unsigned key) { gCalls += "pal_sup;"; return key == gConsumeKey && key == 2; }
bool PortCePaletteOpen(unsigned key, bool) { gCalls += "pal_open;"; return key == gConsumeKey && key == 3; }
bool PortCeCmdKey(unsigned key, bool) { gCalls += "cmd_key;"; return key == gConsumeKey && key == 4; }
void PortCeEditorRender() { gCalls += "editor_overlay;"; }
extern "C" void PortCeEditorFilterSave(CCareer *, void *) { gCalls += "editor_save;"; }
void PortCeBonusFilterSave(CCareer *, void *) { gCalls += "bonus_save;"; }
extern "C" void PortCeMuseumVisitFilterSave(CCareer *, void *) { gCalls += "museum_save;"; }
CCamera *PortCeCmdRenderCamera(CCamera *c, CPlayer *) { gCalls += "cmd_cam;"; return c; }
CCamera *PortCePlayUnitRenderCamera(CCamera *c, CPlayer *) { gCalls += "playunit_cam;"; return c; }
#endif
int main() {
    ce_hook_boot(); ce_hook_exit(CE_EXIT_SHELL); ce_hook_exit(CE_EXIT_ATEXIT); ce_hook_level_begin(100); ce_hook_level_end(100, 0);
    ce_hook_tick_pre(); ce_hook_tick_post(); CeHookCommand command = {1, 2, 3, 0, 0, 0}; ce_hook_input_actions(nullptr, 0); ce_hook_draw_world(nullptr, 0); ce_hook_draw_hud(nullptr, 0);
    ce_hook_draw_overlay(); CeMenuPage page = {CE_MENU_PAUSE_LIST, -1, nullptr, nullptr, nullptr, -1}; ce_hook_menu_rows(CE_MENU_BUILD, &page);
    char buffer[8] = {1, 2, 3, 4, 5, 6, 7, 8}; ce_hook_save_gate(CE_SAVE_CAREER, nullptr, buffer, 8);
    ce_hook_save_gate(CE_SAVE_OPTIONS_FILE, nullptr, buffer, 8);
    ce_hook_audio_clock(CE_AUDIO_REFILL, 4096);
    for (int i = 0; i < 8; i++) if (buffer[i] != i + 1) return 2;
    if (ce_hook_key_capture(0x57, false)) return 3;
    CCamera *camera = reinterpret_cast<CCamera *>(&page); if (ce_hook_render_camera(0, camera, nullptr) != camera) return 4;
    CBattleEngine *be = reinterpret_cast<CBattleEngine *>(buffer); if (ce_hook_hud_focus(nullptr, be) != be) return 5;
#ifdef CE_VERSION
    if (gCalls != "palette_end;console_end;photo_end;cmd_end;gamepad;editor_overlay;editor_save;bonus_save;museum_save;photo_key;pal_sup;pal_open;cmd_key;cmd_cam;playunit_cam;") return 10;
    gCalls.clear(); gConsumeKey = 2;
    if (!ce_hook_key_capture(2, false) || gCalls != "photo_key;pal_sup;") return 11;
    if (!ce_hook_command_push(command)) return 6;
    ce_hook_tick_post();   // applied (to no handler) and emptied
    for (int i = 0; i < CE_HOOK_COMMANDS; i++) if (!ce_hook_command_push(command)) return 7;
    if (ce_hook_command_push(command)) return 8;   // full
    ce_hook_tick_post();
#else
    if (ce_hook_command_push(command)) return 9;   // a private build queues nothing
#endif
    std::puts("inert"); return 0;
}
'''
        base = Path(tempfile.mkdtemp(prefix="inert-", dir=SCRATCH))
        try:
            for flavour, defines in (("ce", ('-DCE_VERSION="hooks-test"',)), ("private", ())):
                with self.subTest(flavour=flavour):
                    out = base / flavour
                    rc, log = compile_host(out, [HOOKS, MODES], defines, driver)
                    self.assertEqual(rc, 0, log)
                    run = subprocess.run([str(out / "driver")], capture_output=True, text=True)
                    self.assertEqual((run.returncode, run.stdout.strip()), (0, "inert"))
        finally:
            shutil.rmtree(base, ignore_errors=True)


class SiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.texts = build.patched_text(SITE_FILES)
        cls.queue = {p.stem for p in (PORT / "patches").glob("*.patch")}

    def test_each_point_is_called_once_per_named_site_and_nowhere_else(self):
        self.assertEqual(site_problems(self.texts, self.queue), [])

    def test_no_other_mirror_file_calls_a_point(self):
        # the site files are checked above; every other patch must leave the points alone
        for patch in sorted((PORT / "patches").glob("*.patch")):
            added = [l for l in patch.read_text(encoding="latin-1").splitlines() if l.startswith("+") and not l.startswith("+++")]
            calls = [l for l in added if re.search(r"\bce_hook_\w+\s*\(", l.split("//", 1)[0])]
            if calls:
                self.assertTrue(patch.stem.startswith("03") and patch.stem[5:].startswith("ce-hook-"), patch.name)

    def test_the_rules_catch_a_missing_site_a_second_call_and_a_missing_patch(self):
        texts = dict(self.texts)
        texts["Hud.cpp"] = texts["Hud.cpp"].replace("\tce_hook_draw_hud(player, viewpoint);", "\t;", 1)
        self.assertTrue(any("draw_hud: Hud.cpp" in p and "0 calls" in p for p in site_problems(texts, self.queue)))
        texts = dict(self.texts)
        texts["game.cpp"] = texts["game.cpp"].replace("\tce_hook_tick_pre();", "\tce_hook_tick_pre(); ce_hook_tick_pre();", 1)
        self.assertTrue(any("tick_pre: game.cpp CGame::Update has 2 calls" in p for p in site_problems(texts, self.queue)))
        texts = dict(self.texts)
        texts["Career.cpp"] = texts["Career.cpp"].replace("\tif (PortCeCareerSaving(this))", "\tce_hook_save_gate(CE_SAVE_CAREER, this, dest, 0); if (PortCeCareerSaving(this))", 1)
        texts["Career.cpp"] = texts["Career.cpp"].replace("\tce_hook_save_gate(CE_SAVE_CAREER, this, dest, sizeof(CCareer));", "\t;", 1)
        self.assertEqual(site_problems(texts, self.queue), [], "a call moved within its function still counts as its site (order is checked below)")
        self.assertTrue(any("0313-ce-hook-save-gate" in p for p in site_problems(self.texts, self.queue - {"0313-ce-hook-save-gate"})))
        texts = dict(self.texts)
        texts["Controller.cpp"] = texts["Controller.cpp"] + "\nvoid Elsewhere()\n{\n\tce_hook_input_actions(0, 0);\n}\n"
        self.assertTrue(any("unexpected call in Controller.cpp Elsewhere" in p for p in site_problems(texts, self.queue)))

    def order(self, name, *tokens):
        text = self.texts[name]
        places = [text.index(t) for t in tokens]
        self.assertEqual(places, sorted(places), f"{name}: {tokens}")

    def test_each_call_keeps_todays_order(self):
        rows = table(HOOKS.read_text())
        self.order("Career.cpp", "PortCeCareerSaving(this)", "memcpy(dest, this, sizeof(CCareer));", "ce_hook_save_gate(CE_SAVE_CAREER",
                   "SetIsGod(0, portGod0);")
        self.assertEqual([h[1] for h in rows["save_gate"][2]], ["editor_save", "bonus_save", "museum_visit_save"])
        self.order("ltshell.cpp", "PortCeEditorKey(key", "ce_hook_key_capture(", "mCurrentKeytrap(key,KEY_EVENT_DOWN)")
        self.assertEqual([h[1] for h in rows["key_capture"][2]], ["photo_key", "palette_suppress_down", "palette_open", "commander_key"])
        self.order("ltshell.cpp", "CE_BootStart()", "MEM_MANAGER.Init(", "LT.mhInstance=hInstance;", "ce_hook_boot();", "PortCeGraphicsOptionsOpened(")
        self.order("ltshell.cpp", "if (!sMemoryManagerReady)", "ce_hook_exit(CE_EXIT_ATEXIT)", "PATCHMANAGER.FreeData();")
        self.order("ltshell.cpp", "ce_hook_exit(CE_EXIT_SHELL)", "PortSessionSummary();\n#ifdef CE_NETWORK_OFF")
        self.order("game.cpp", "\tPortCeEditorLevelEnd();\n", "\tPortCeMuseumVisitLevelEnd();\n", "ce_hook_level_end(", "\t\tMUSIC.Stop();\n\n\t// we don")
        self.assertEqual([h[1] for h in rows["level_end"][2]], ["palette_close", "console_level_end", "photo_level_end", "commander_level_end"])
        self.order("game.cpp", "{ PORT_UPDATE_HOOK();", "\tce_hook_tick_pre();", "\tce_hook_tick_post();", "void\tCGame::PreRun()")
        self.assertEqual([h[1] for h in rows["tick_pre"][2]], ["gamepad_cycle"])
        self.order("game.cpp", "PortCeGamepadBeginCycle();\n#endif\n\t\tmController[0]->Flush();", "ce_hook_level_begin(aLevel);", "CE_SessionPreparePlay()")
        self.order("Controller.cpp", "PortCeDodgeAssistEnd(this", "ce_hook_input_actions(this, pad);", "RecordControllerState() ;\n\t}")
        # the hall draw runs earlier in Render than draw_fairly_late_stuff, which Render calls at its end
        self.order("DXEngine.cpp", "PortCeMuseumHallDraw();\t//", "draw_fairly_late_stuff(mPlayer[mCurrentViewpoint], viewpoint);")
        self.order("DXEngine.cpp", "RENDERINFO.GetProjection(&ENGINE.mProjectionMatrices[viewpoint]);",
                   "ce_hook_draw_world(", "be->GetCockpit()->Render()", "SCREENFX.Render();")
        self.order("DXEngine.cpp", "PortCePaletteRender();", "PortCeNoticeRender();", "PortCeCmdRender();\t//",
                   "ce_hook_draw_overlay();", "PLATFORM.EndScene();\n\n\tmFirstFrame")
        self.assertEqual([h[1] for h in rows["draw_overlay"][2]], ["editor_overlay"])
        self.order("Hud.cpp", "if (PortCeCmdHideCockpit()) return;", "RenderRadar(96.f);", "ce_hook_draw_hud(")
        self.order("PauseMenu.cpp", "PortCePhotoItem()", "CMenuItem((DWORD)GI_QUIT, 2));\n#ifdef", "CE_MENU_PAUSE_LIST", "mLists.Append(list);\n\n\t// the controls",
                   "PortCeOptionsMenu(this, true);", "CE_MENU_PAUSE_OPTIONS")
        self.order("FrontEnd.cpp", "mPages[FEP_LANGUAGE_TEST] = PortCePage();", "CE_MENU_FRONTEND_PAGES", "// Initialise all pages")
        self.order("engine.cpp", "ce_hook_render_camera(", "memcpy(&mViewport[viewpoint]")
        self.assertEqual([h[1] for h in rows["render_camera"][2]], ["commander_camera", "playunit_camera"])
        self.order("PCMusicStream.cpp", "PortAudioPublishMusicHalf(", "PortRefillReport(offset,portRefillBefore);", "\t\t\tsBytesStreamed += size1;\n\t\t\tce_hook_audio_clock(CE_AUDIO_REFILL", "\t\t\tbreak;\n\n\t\t}\n\t\tcase 3:")

    def test_the_order_check_catches_a_moved_call(self):
        call = "\tce_hook_save_gate(CE_SAVE_CAREER, this, dest, sizeof(CCareer));\t// CE hook point (port/game/ce_hooks.h): after the career copy, before the god flags return\n"
        moved = self.texts["Career.cpp"].replace(call, "", 1).replace(
            "\tSetIsGod(0, portGod0);\n", "\tSetIsGod(0, portGod0);\n" + call, 1)
        saved, self.texts["Career.cpp"] = self.texts["Career.cpp"], moved
        try:
            with self.assertRaises(AssertionError):
                self.order("Career.cpp", "memcpy(dest, this, sizeof(CCareer));", "ce_hook_save_gate(CE_SAVE_CAREER", "SetIsGod(0, portGod0);")
        finally:
            self.texts["Career.cpp"] = saved

DISPATCH_DRIVER = r"""
#include "ce_hooks_dispatch.h"
#include <cstdio>
#include <cstring>
#include <string>
static bool gLayer = true;
bool PortCeLayer() { return gLayer; }
static std::string gLog;
static int failures;
static void Check(bool ok, const char *what) { if (!ok) { std::printf("FAIL %s (log %s)\n", what, gLog.c_str()); failures++; } }
#if CE_HOOKS_GUARDED
static std::string gGuard;
void CeHookGuardBefore(CeHookGuard *g) { gGuard += std::string("<") + g->point + ":" + g->handler + (g->tag == CE_HOOK_RENDER ? "R" : "S"); g->state = 42; }
void CeHookGuardAfter(CeHookGuard *g) { gGuard += g->state == 42 ? ">" : "!"; }
#endif
typedef void (*V)(int);
static void A(int v) { gLog += "A" + std::to_string(v); }
static void B(int v) { gLog += "B" + std::to_string(v); }
static void C(int v) { gLog += "C" + std::to_string(v); }
static constexpr CeHookRow<V> kBroadcast[] = {{10, "a", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, A},
    {20, "b", CE_HOOK_RENDER, CE_GATE_LAYER, CE_MODE_NONE, B}, {30, "c", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_EDITOR, C}, CE_HOOK_END};
CE_HOOK_CHECK(kBroadcast, CE_HOOK_RENDER, CE_DISPATCH_ORDERED_BROADCAST, "broadcast");
typedef bool (*K)(int);
static bool K1(int k) { gLog += "1"; return k == 1; }
static bool K2(int k) { gLog += "2"; return k == 2; }
static bool K3(int) { gLog += "3"; return true; }
static constexpr CeHookRow<K> kFirst[] = {{10, "k1", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, K1},
    {20, "k2", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, K2}, {30, "k3", CE_HOOK_RENDER, CE_GATE_LAYER, CE_MODE_NONE, K3}, CE_HOOK_END};
CE_HOOK_CHECK(kFirst, CE_HOOK_RENDER, CE_DISPATCH_FIRST_MATCH, "first");
typedef void (*F)(char *);
static void F1(char *s) { std::strcat(s, "+editor"); }
static void F2(char *s) { if (std::strstr(s, "+editor")) std::strcat(s, "+bonus-saw-editor"); else std::strcat(s, "+bonus"); }
static constexpr CeHookRow<F> kFilter[] = {{10, "f1", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, F1}, {20, "f2", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, F2}, CE_HOOK_END};
CE_HOOK_CHECK(kFilter, CE_HOOK_SIM, CE_DISPATCH_FILTER, "filter");
typedef int (*O)(int, int);
static int O1(int v, int add) { return v * 10 + add; }
static int O2(int v, int) { return v + 1; }
static constexpr CeHookRow<O> kOverride[] = {{10, "o1", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, O1}, {20, "o2", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, O2}, CE_HOOK_END};
CE_HOOK_CHECK(kOverride, CE_HOOK_SIM, CE_DISPATCH_OVERRIDE_CHAIN, "override");
typedef void (*Q)(const CeHookCommand *);
static void Q1(const CeHookCommand *c) { gLog += "q" + std::to_string(c->kind); }
static void Q2(const CeHookCommand *c) { gLog += "r" + std::to_string(c->a); }
static constexpr CeHookRow<Q> kApply[] = {{10, "q1", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, Q1}, {20, "q2", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, Q2}, CE_HOOK_END};
CE_HOOK_CHECK(kApply, CE_HOOK_SIM, CE_DISPATCH_COMMAND_APPLY, "apply");
static constexpr CeHookRow<V> kEmpty[] = {CE_HOOK_END};
CE_HOOK_CHECK(kEmpty, CE_HOOK_SIM, CE_DISPATCH_ORDERED_BROADCAST, "empty");
int main() {
    CeHookBroadcast("broadcast", kBroadcast, 7); Check(gLog == "A7B7", "broadcast: table order; the editor-mode row waits for its mode");
    gLog.clear(); gLayer = false; CeHookBroadcast("broadcast", kBroadcast, 1); Check(gLog == "A1", "broadcast: a LAYER row is shut while PortCeLayer is false");
    gLayer = true; CeModeBegin(CE_MODE_EDITOR); gLog.clear(); CeHookBroadcast("broadcast", kBroadcast, 2); Check(gLog == "A2B2C2", "broadcast: the mode row runs while its mode is active");
    CeModeEnd(CE_MODE_EDITOR);
    gLog.clear(); Check(CeHookFirstMatch("first", kFirst, 2) && gLog == "12", "first match: stops at the first that consumes");
    gLog.clear(); Check(CeHookFirstMatch("first", kFirst, 9) && gLog == "123", "first match: the last consumes");
    gLayer = false; gLog.clear(); Check(!CeHookFirstMatch("first", kFirst, 9) && gLog == "12", "first match: nothing consumed");
    gLayer = true;
    char buffer[64] = "career"; CeHookFilter("filter", kFilter, buffer); Check(!std::strcmp(buffer, "career+editor+bonus-saw-editor"), "filter: each sees the previous output");
    Check(CeHookOverride("override", kOverride, 3, 5) == 36, "override chain: each takes the previous answer, in order");
    Check(CeHookOverride("empty", kOverride + 2, 3, 5) == 3, "override chain: no row gives the value back");
    CeHookCommandQueue queue = {}; CeHookCommand c1 = {1, 11, 0, 0, 0, 0}, c2 = {2, 22, 0, 0, 0, 0}, c0 = {0, 0, 0, 0, 0, 0};
    Check(queue.Push(c1) && queue.Push(c2) && !queue.Push(c0), "commands: kind 0 is refused");
    gLog.clear(); CeHookApply("apply", kApply, &queue); Check(gLog == "q1r11q2r22" && queue.count == 0, "command apply: oldest first, every row per command, then emptied");
    gLog.clear(); CeHookApply("apply", kApply, &queue); Check(gLog.empty(), "command apply: an empty queue runs nothing");
    Check(CeHookPlace(3, 1) == 1 && CeHookPlace(3, -1) == 3 && CeHookPlace(3, 9) == 3 && CeHookPlace(0, 0) == 0, "positions");
#if CE_HOOKS_GUARDED
    gGuard.clear(); gLog.clear(); CeHookBroadcast("broadcast", kBroadcast, 1); CeHookBroadcast("empty", kEmpty, 1);
    Check(gGuard == "<broadcast:aR><broadcast:bR>", "guard: before and after each handler that runs, with its tag; none for an empty table");
    gGuard.clear(); CeHookFirstMatch("first", kFirst, 1); Check(gGuard == "<first:k1R>", "guard: around a first match");
#endif
    if (!failures) std::puts("dispatch ok");
    return failures != 0;
}
"""


class DispatchTests(unittest.TestCase):
    def run_driver(self, name, defines, driver=DISPATCH_DRIVER):
        base = SCRATCH / name
        shutil.rmtree(base, ignore_errors=True)
        rc, log = compile_host(base, [MODES], defines, driver)
        self.assertEqual(rc, 0, log)
        run = subprocess.run([str(base / "driver")], capture_output=True, text=True)
        shutil.rmtree(base, ignore_errors=True)
        return run.returncode, run.stdout

    def test_each_dispatch_does_what_it_says(self):
        for flavour, defines in (("public", ('-DCE_VERSION="hooks-test"',)), ("test-build", ('-DCE_VERSION="hooks-test"', "-DCE_TEST_BUILD=1"))):
            with self.subTest(flavour=flavour):
                self.assertEqual(self.run_driver("dispatch-" + flavour, defines), (0, "dispatch ok\n"))

    def test_the_driver_catches_broken_dispatch(self):
        source = (PORT / "game/ce_hooks_dispatch.h").read_text()
        mutants = {
            "first match runs on": ("\t\tif (CeHookOpen(*r) && CeHookRun(point, *r, [&](Fn fn) { return fn(args...); }))\n\t\t\treturn true;",
                                    "\t\tif (CeHookOpen(*r)) CeHookRun(point, *r, [&](Fn fn) { return fn(args...); });"),
            "override drops the chain": ("value = CeHookRun(point, *r, [&](Fn fn) { return fn(value, args...); });",
                                         "CeHookRun(point, *r, [&](Fn fn) { return fn(value, args...); });"),
            "queue not emptied": ("\tqueue->count = 0;\n", "\n"),
            "gate ignored": ("return (row.gate == CE_GATE_VERSION || PortCeLayer()) && CeModeActive(row.mode);", "return CeModeActive(row.mode);"),
            "mode ignored": ("return (row.gate == CE_GATE_VERSION || PortCeLayer()) && CeModeActive(row.mode);", "return row.gate == CE_GATE_VERSION || PortCeLayer();"),
            "no guard after": ("\t\t~Scope() { CeHookGuardAfter(&guard); }\n", "\n"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                base = SCRATCH / ("mutant-" + name.replace(" ", "-"))
                shutil.rmtree(base, ignore_errors=True)
                (base / "inc").mkdir(parents=True)
                (base / "inc/ce_hooks_dispatch.h").write_text(source.replace(old, new))
                shutil.copy(PORT / "game/ce_modes.h", base / "inc/ce_modes.h")
                (base / "driver.cpp").write_text(DISPATCH_DRIVER)
                built = subprocess.run([shutil.which("g++") or "c++", "-std=gnu++14", '-DCE_VERSION="m"', "-DCE_TEST_BUILD=1", "-I", str(base / "inc"),
                                        str(base / "driver.cpp"), str(MODES), "-o", str(base / "driver")], capture_output=True, text=True)
                caught = built.returncode != 0 or subprocess.run([str(base / "driver")], capture_output=True).returncode != 0
                shutil.rmtree(base, ignore_errors=True)
                self.assertTrue(caught, f"{name} was not caught")


class IndependenceTests(unittest.TestCase):
    def test_each_site_patch_applies_without_any_one_other_patch(self):
        patches = sorted((PORT / "patches").glob("*.patch"))
        problems = []
        for f in SITE_FILES:
            touching = [p for p in patches if not p.name.startswith("03") and re.search(r"^--- a/" + re.escape(f) + r"$", p.read_text(encoding="latin-1"), re.M)]
            for skip in touching:
                hook_patches = [p.stem for p in patches if p.name.startswith("03") and re.search(r"^--- a/" + re.escape(f) + r"$", p.read_text(encoding="latin-1"), re.M)]
                allowed = tuple(h + ".patch" for h in hook_patches if (h, skip.stem) in DEPENDS)
                try:
                    build.patched_text([f], skip=(skip.name,) + allowed)
                except RuntimeError as error:
                    if str(error).startswith("03"):
                        problems.append(f"{f}: without {skip.name}: {str(error).splitlines()[0]}")
        self.assertEqual(problems, [])



MODES_DRIVER = r'''
#include "ce_modes.h"
#include <cstdio>
static int failures;
static void Check(bool ok, const char *what) { if (!ok) { std::printf("FAIL %s\n", what); failures++; } }
int main() {
    Check(CeModeActive(CE_MODE_NONE), "NONE is always active");
    Check(CeModeActiveMask() == 0, "nothing active at start");
    Check(CeModeBegin(CE_MODE_EDITOR) == CE_MODE_BEGUN, "editor begins");
    Check(CeModeActive(CE_MODE_EDITOR), "editor active");
    Check(CeModeBegin(CE_MODE_EDITOR) == CE_MODE_ALREADY_ACTIVE, "a second begin is refused");
    Check(CeModeBegin(CE_MODE_MUSEUM_VISIT) == CE_MODE_CONFLICT, "a museum visit during the editor is refused");
    Check(!CeModeActive(CE_MODE_MUSEUM_VISIT), "a refused begin changes nothing");
    Check(CeModeConflictWith(CE_MODE_BONUS_LEVEL) == CE_MODE_EDITOR, "the conflict names the editor");
    Check(CeModeBegin(CE_MODE_PROJECT) == CE_MODE_BEGUN, "a project during the editor begins (the one compatible pair)");
    Check(CeModeActiveMask() == ((1u << CE_MODE_EDITOR) | (1u << CE_MODE_PROJECT)), "mask");
    Check(CeModeBegin(CE_MODE_COMMANDER) == CE_MODE_CONFLICT, "commander during the editor is refused");
    Check(CeModeEnd(CE_MODE_EDITOR), "editor ends");
    Check(!CeModeEnd(CE_MODE_EDITOR), "a second end reports false");
    Check(CeModeBegin(CE_MODE_COMMANDER) == CE_MODE_CONFLICT, "commander during a project is refused");
    Check(CeModeEnd(CE_MODE_PROJECT), "project ends");
    Check(CeModeBegin(CE_MODE_COMMANDER) == CE_MODE_BEGUN, "commander alone begins");
    Check(CeModeBegin(CE_MODE_PLAY_UNIT) == CE_MODE_CONFLICT, "play-unit during commander is refused");
    Check(CeModeBegin(CE_MODE_NONE) == CE_MODE_INVALID && CeModeBegin(CE_MODE_COUNT) == CE_MODE_INVALID, "invalid modes");
    Check(CeModesCompatible(CE_MODE_PROJECT, CE_MODE_EDITOR) && !CeModesCompatible(CE_MODE_BONUS_LEVEL, CE_MODE_MUSEUM_VISIT), "symmetric pairs");
    if (!failures) std::puts("modes ok");
    return failures != 0;
}
'''


class ModeTests(unittest.TestCase):
    def run_modes(self, source_text, name):
        base = SCRATCH / name
        shutil.rmtree(base, ignore_errors=True)
        base.mkdir(parents=True)
        source = base / "ce_modes.cpp"
        source.write_text(source_text)
        rc, log = compile_host(base, [source], (), MODES_DRIVER)
        self.assertEqual(rc, 0, log)
        run = subprocess.run([str(base / "driver")], capture_output=True, text=True)
        shutil.rmtree(base, ignore_errors=True)
        return run.returncode, run.stdout

    def test_begin_end_and_conflicts(self):
        self.assertEqual(self.run_modes(MODES.read_text(), "modes"), (0, "modes ok\n"))

    def test_the_driver_catches_a_registry_without_its_conflict_check(self):
        text = MODES.read_text()
        check = "\tif (CeModeConflictWith(mode) != CE_MODE_NONE)\n\t\treturn CE_MODE_CONFLICT;\n"
        self.assertEqual(text.count(check), 1)
        rc, out = self.run_modes(text.replace(check, ""), "modes-mutant")
        self.assertNotEqual(rc, 0)
        self.assertIn("FAIL", out)


HAZARD_FILTERS = (("editor", "port/game/ce_editor.cpp", "void PortCeEditorFilterSave(", "memcpy(destination, &restored, sizeof(restored));"),
                  ("bonus level", "port/game/ce_bonus.cpp", "void PortCeBonusFilterSave(", "memcpy(dest, (const void *)&filtered, sizeof(filtered));"),
                  ("museum visit", "port/game/ce_museum_visit_engine.cpp", "void PortCeMuseumVisitFilterSave(", "std::memcpy(dest, (const void *)&saved, sizeof(saved));"))


class SaveHazardTests(unittest.TestCase):
    """CCareer::Save runs three filters in a row (patches 0222, 0181, 0279); each writes ITS OWN whole snapshot over the
    copy, so if two were active at once the later one's snapshot would be written and the earlier one's lost. Whether two
    can be active together is not shown; one owner of the career snapshot is the later change. Kept order: the hook point
    runs after all three."""

    def test_each_filter_replaces_the_whole_copy_in_todays_order(self):
        for _, path, start, write in HAZARD_FILTERS:
            body = (ROOT / path).read_text()
            body = body[body.index(start):]
            body = body[:body.index("\n}\n")]
            self.assertIn(write, body, f"{path}: the filter no longer writes a whole snapshot (re-read the hazard)")
        text = build.patched_text(["Career.cpp"])["Career.cpp"]
        self.assertLess(text.index("memcpy(dest, this, sizeof(CCareer));"), text.index("ce_hook_save_gate(CE_SAVE_CAREER, this, dest, sizeof(CCareer));"))
        self.assertLess(text.index("ce_hook_save_gate(CE_SAVE_CAREER, this, dest, sizeof(CCareer));"), text.index("SetIsGod(0, portGod0);"))
        rows = table(HOOKS.read_text())["save_gate"][2]
        self.assertEqual([h[5] for h in rows], ["CeHookEditorSave", "CeHookBonusSave", "CeHookMuseumVisitSave"])

    @unittest.expectedFailure
    def test_two_active_filters_keep_both_snapshots(self):
        # EXPECTED FAILURE (the save-filter order hazard; a later change gives the career snapshot one owner). The model
        # follows the three filters: each, when active, replaces the copy with its snapshot (the live options aside).
        live = {"progress": "live"}
        snapshots = {"editor": {"progress": "editor's start"}, "bonus level": {"progress": "bonus entry"},
                     "museum visit": {"progress": "museum entry"}}
        order = [name for name, _, _, _ in HAZARD_FILTERS]
        for i, first in enumerate(order):
            for second in order[i + 1:]:
                copy = dict(live)
                for name in order:
                    if name in (first, second):
                        copy = dict(snapshots[name])
                with self.subTest(active=(first, second)):
                    self.assertIn(copy["progress"], (snapshots[first]["progress"],), f"{first}'s snapshot lost to {second}'s")


HOOK_DECL = re.compile(r"^[A-Za-z_][\w \t\*]*?\b(ce_hook_\w+)\s*\(", re.M)
STUB_DEF = re.compile(r"\b(ce_hook_\w+)\s*\([^;{]*\)\s*\{")


def missing_stubs(header_text, stubs_text):
    """The hook functions ce_hooks.h declares that the host stub header does not define."""
    declared = set(HOOK_DECL.findall(header_text))
    defined = set(STUB_DEF.findall(stubs_text))
    return sorted(declared - defined), sorted(defined - declared)


class HostStubTests(unittest.TestCase):
    """port/game/ce_hooks_host_stubs.h defines every hook point for host test drivers, and compiles on the host; a hook
    added without its stub fails here, not in the fourteen suites whose drivers compile a hook site."""

    def test_every_hook_has_a_stub_and_no_stub_is_stale(self):
        header = (PORT / "game" / "ce_hooks.h").read_text()
        stubs = (PORT / "game" / "ce_hooks_host_stubs.h").read_text()
        missing, stale = missing_stubs(header, stubs)
        self.assertGreaterEqual(len(HOOK_DECL.findall(header)), 17, "the declaration pattern no longer finds the hooks")
        self.assertEqual(missing, [], "hook points without a host stub")
        self.assertEqual(stale, [], "host stubs for hook points that no longer exist")
        # mutants: drop one stub, add one hook
        self.assertEqual(missing_stubs(header, stubs.replace("void\t\t\tce_hook_draw_overlay() {}", ""))[0], ["ce_hook_draw_overlay"])
        self.assertEqual(missing_stubs(header + "\nvoid\t\t\tce_hook_new_point(int x);\n", stubs)[0], ["ce_hook_new_point"])

    def test_the_stubs_compile_and_do_nothing(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
            src = Path(tmp) / "stub.cpp"
            src.write_text('#include "ce_hooks_host_stubs.h"\nint main() {\n'
                           '  CCamera *c = (CCamera *)8; CBattleEngine *b = (CBattleEngine *)16;\n'
                           '  return (ce_hook_key_capture(1, false) || ce_hook_render_camera(0, c, 0) != c\n'
                           '          || ce_hook_hud_focus(0, b) != b) ? 1 : 0;\n}\n')
            built = subprocess.run([shutil.which("g++") or "c++", "-std=gnu++14", "-Wall", "-Werror", "-I", str(PORT / "game"),
                                    str(src), "-o", str(Path(tmp) / "stub")], capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stderr)
            self.assertEqual(subprocess.run([str(Path(tmp) / "stub")], timeout=30).returncode, 0, "a stub did something")


class TestBuildGuardTests(unittest.TestCase):
    """The handler guard is compiled in exactly the test builds: build.py defines CE_TEST_BUILD from its test switches, and
    the guard names no single test flavour (so a new test switch gets the guard without editing the hook code)."""

    def test_build_py_derives_the_define_from_the_test_switches(self):
        text = (PORT / "build.py").read_text()
        self.assertEqual(text.count('CE_DEFINES.append("-DCE_TEST_BUILD=1")'), 1, "build.py must define CE_TEST_BUILD once")
        self.assertRegex(text, r'if test_build_switches\(CE_DEFINES\):\n\s+CE_DEFINES\.append\("-DCE_TEST_BUILD=1"\)',
                         "CE_TEST_BUILD must follow from the test switches, not from one flag")
        defines = ["-DCE_VERSION=\"x\""]
        self.assertEqual(build.test_build_switches(defines), [], "a public build has no test switch, so no guard")
        self.assertEqual(build.test_build_switches(defines + ["-DCE_PLAYUNIT_TEST=1"]), ["play-unit-test"])

    def test_the_guard_names_no_single_test_flavour(self):
        for name in ("ce_hooks_dispatch.h", "ce_hooks_check.cpp", "ce_hooks.cpp", "ce_hooks.h"):
            text = (PORT / "game" / name).read_text()
            for define, _ in build.TEST_BUILD_SWITCHES:
                self.assertNotIn(define[2:], text, f"{name} names {define[2:]}: use CE_TEST_BUILD")
        self.assertIn("defined(CE_TEST_BUILD)", (PORT / "game" / "ce_hooks_dispatch.h").read_text())


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
