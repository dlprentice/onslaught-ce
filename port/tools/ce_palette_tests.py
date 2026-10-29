#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual CE console (port/game/ce_palette.cpp), its patched callers and the patched retail console print
with authored fakes; no game, device or network. Causal mutants of the console must each be caught."""
import os
from pathlib import Path
import re
import resource
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402
PATCHES = ROOT / "port/patches"
DRIVER = ROOT / "port/tools/ce_palette_driver.cpp"
FAKE = ROOT / "port/tools/ce_palette_fake.h"
PALETTE = ROOT / "port/game/ce_palette.cpp"
CONSOLE_H = ROOT / "port/game/ce_console.h"
MUSIC_H = ROOT / "port/game/ce_music.h"
PHOTO_H = ROOT / "port/game/ce_photo.h"
ACTIONS = ROOT / "port/game/ce_console_game.cpp"
FLAGS = ("BEA_PORT_CE_LAYER", "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
         "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN")
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[start:end]


def sections(patch):
    return {m.group(1): m.group(0) for m in re.finditer(
        r"(?ms)^--- a/([^\n]+)\n\+\+\+ b/[^\n]+\n.*?(?=^--- a/|\Z)", patch)}


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def mirror_text(name):
    """src/NAME as the port's mirror holds it before any patch: include names and their case normalised as build.py does.
    The patches' context lines are written against that text ("common.h", not the original's "Common.h")."""
    build.SYSTEM_NAMES.update(build.system_header_names())
    rows = [l.split(maxsplit=1) for l in (ROOT / "config/game-data.sha256").read_text().splitlines() if l.strip()]
    targets = ([build.MIRROR_ROOT / f.relative_to(ROOT) for f in build.tracked_sources()] +
               [build.MIRROR / r for _, r in rows] + [build.MIRROR / "data/NVShaderFragments.inc"])
    raw = (ROOT / "src" / name).read_bytes().decode("latin-1")
    return build.normalise(raw, build.MIRROR_ROOT / "src", build.CaseIndex(targets),
                           [build.MIRROR, build.MIRROR / "MissionScript"]).encode("latin-1")


def patched(out, names):
    """The mirror's copies of names with every port patch's section for them applied in the build's order, at fuzz 0."""
    for name in names:
        (out / name).write_bytes(mirror_text(name))
    for patch in sorted(PATCHES.glob("*.patch")):
        parts = sections(patch.read_text(encoding="latin1"))
        for name in names:
            if name in parts:
                (out / "section.patch").write_text(parts[name], encoding="latin1")
                applied = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                                         capture_output=True, text=True, timeout=10, preexec_fn=no_core)
                if applied.returncode or "fuzz" in applied.stdout.lower():
                    raise AssertionError(f"{patch.name} {name}: {applied.stdout}{applied.stderr}")
    return {name: (out / name).read_text(encoding="latin1") for name in names}


class PaletteTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-palette-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def fixture(self, folder, palette_text):
        folder.mkdir(exist_ok=True)
        files = patched(folder, ["ltshell.cpp"])
        (folder / "msgproc.inc").write_text(function(files["ltshell.cpp"], "LRESULT PCLTShell::MsgProc("))
        (folder / "ce_layer.inc").write_text(function((ROOT / "port/platform/common/ce.cpp").read_text(), "bool PortCeLayer()"))
        (folder / "ce_palette.cpp").write_text(palette_text)
        (folder / "ce_console.h").write_bytes(CONSOLE_H.read_bytes())
        (folder / "ce_music.h").write_bytes(MUSIC_H.read_bytes())
        (folder / "ce_photo.h").write_bytes(PHOTO_H.read_bytes())
        (folder / "ce_palette_fake.h").write_bytes(FAKE.read_bytes())
        for name in ("common.h", "game.h", "frontend.h", "PauseMenu.h", "CLIParams.h", "ltshell.h", "DXEngine.h",
                     "Hud.h", "SpriteRenderer.h", "renderinfo.h", "Texture.h", "GameInterface.h"):
            (folder / name).write_text('#include "ce_palette_fake.h"\n')

    def build(self, folder, name, ce, network=False, sanitize=True):
        flags = [os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror",
                 *(SANITIZE if sanitize else []), *(["-DCE_VERSION=1"] if ce else []), "-I", str(folder)]
        extra = []
        if network:
            real_network = ROOT / "port/game/ce_network_boot.cpp"
            if real_network.is_file():
                boot = real_network.read_text()
                accessor = function(boot, 'extern "C" int CE_BootRequested(void)')
                self.assertIn("__atomic_load_n(&requested, __ATOMIC_ACQUIRE)", accessor)
                self.assertIn("__atomic_store_n(&requested, 1, __ATOMIC_RELEASE)", boot)
            else:
                accessor = 'extern "C" int CE_BootRequested(void) { return requested; }'
            fixture = folder / "network_boot.cpp"
            fixture.write_text('namespace { int requested; }\n' + accessor +
                               '\nextern "C" void SetBootRequested(bool v) '
                               '{ __atomic_store_n(&requested, v ? 1 : 0, __ATOMIC_RELEASE); }\n')
            extra = ["-DFAKE_NETWORK=1", str(fixture)]
        exe = folder / name
        built = subprocess.run(flags + extra + [str(DRIVER), str(folder / "ce_palette.cpp"), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)
        return exe

    def run_driver(self, exe, mode="ordinary", changes=None):
        env = {k: v for k, v in os.environ.items() if k not in FLAGS}
        env.update(changes or {})
        env.update(ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        return subprocess.run([str(exe), mode], env=env, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def test_actual_shell_console_and_modes(self):
        folder = self.out / "actual"
        self.fixture(folder, PALETTE.read_text())
        plain = self.build(folder, "ce", True)
        ran = self.run_driver(plain)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("console admission, gates, bounds, undo, panel and key lifetimes passed", ran.stdout)
        denied = self.run_driver(plain, "denied", {"BEA_PORT_CE_LAYER": "0"})
        self.assertEqual(denied.returncode, 0, denied.stdout + denied.stderr)
        for flag in FLAGS[1:]:
            with self.subTest(flag=flag):
                # The CE screenshot override must never enable interactive commands in these modes.
                ran = self.run_driver(plain, "denied", {"BEA_PORT_CE_LAYER": "1", flag: "1"})
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        network = self.build(folder, "network", True, network=True)
        ran = self.run_driver(network)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        faithful = self.build(folder, "faithful", False, sanitize=False)
        ran = self.run_driver(faithful)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("faithful message path passed", ran.stdout)

    def test_mutants_are_caught(self):
        source = PALETTE.read_text()
        mutants = [
            # (name, old, new, what the driver or a sanitizer reports)
            ("input-unbounded", "if (key <= 126 && length < kInputLimit && !overflow) {",
             "if (key <= 126 && length <= kInputLimit && !overflow) {", "out of bounds"),
            ("input-truncates", "} else overflow = true; // never execute truncated or unsupported input",
             "} // truncated", "FAIL"),
            ("draw-unbounded", "for (unsigned i = 0, spaces = 0; text[i] && n < 99; ++i)",
             "for (unsigned i = 0, spaces = 0; text[i] && n < 100; ++i)", "out of bounds"),
            ("no-cheat-gate", "if (c.cheat >= 0 && !CheatOn(c.cheat))", "if (false)", "FAIL"),
            ("wrong-cheat", "const int kCheatLevels = 1;", "const int kCheatLevels = 4;", "FAIL"),
            ("no-tape-fence", '"BEA_PORT_TAPE", "BEA_PORT_PROBES"', '"BEA_UNUSED_TAPE", "BEA_PORT_PROBES"', "FAIL"),
            ("no-network-fence", '"BEA_NET_HOST", "BEA_NET_JOIN"', '"BEA_NET_HOST", "BEA_UNUSED_JOIN"', "FAIL"),
            ("no-undo", "memcpy(data, changes[i].saved, n);", "(void)data;", "FAIL"),
            ("undo-over-game", "&& !memcmp(data, changes[i].set, n))", ")", "FAIL"),
            ("no-navmap-undo", "if (navMapChanged && NavMapOn() != navMapWas) NavMap(navMapWas);", "", "FAIL"),
            ("no-gamma-reset", "if (gammaChanged) SetGammaBias(0.f);", "", "FAIL"),
            ("no-gamma-clamp", "if (v > 0.005f) v = 0.005f;", "", "FAIL"),
            ("film-in-battle", "(void)localise;\n\tOut(\"Films are not played during a battle",
             "snprintf(pendingFilm, sizeof(pendingFilm), \"%s\", file);\n\tpendingLocalise = localise;\n\tOut(\"Films are not played during a battle", "FAIL"),
            ("set-any-variable", "for (const VarRule &r : kVars) if (Same(r.name, name)) return &r;\n\treturn NULL;",
             "for (const VarRule &r : kVars) if (Same(r.name, name)) return &r;\n\tstatic VarRule any; any = {name, VAR_FLOAT, 0, 9, \"\"};"
             " void *d; if (Var(name, &d) == VAR_FLOAT) return &any;\n\treturn NULL;", "FAIL"),
            ("no-range", "(r->type == VAR_FLOAT && (f < r->low || f > r->high)))", "false)", "FAIL"),
            ("split-every-level", "SetHorizontalSplit(pendingSplit != 0);\n\tpendingSplit = -1;",
             "SetHorizontalSplit(pendingSplit != 0);", "FAIL"),
            ("devstats-unfenced", "\tif (!proof)\n", "\tif (false && !proof)\n", "FAIL"),
            ("devstats-proof-without-layer", '&& Present("BEA_PORT_CE_LAYER");', ';', "FAIL"),
            ("devstats-sticky", "\tdevStats = -1;\n}", "}", "FAIL"),
            ("devstats-in-photo", "if (!DevStatsAllowed() || PortCePhotoOn()) return false;", "if (!DevStatsAllowed()) return false;", "FAIL"),
            ("photo-keeps-panel", "\tif (on) Close();\n}", "}", "FAIL"),
            ("photo-ignores-refusal", "if (!PortCePhotoSet(on)) {", "if (!PortCePhotoSet(on) && false) {", "FAIL"),
            ("words-unbounded", "if (argc == int(kArgs)) return -1;", "if (argc == int(kArgs) + 1) return -1;", "out of bounds"),
            ("music-dir-first-word", "snprintf(dir, sizeof(dir), \"%s\", rest);", "snprintf(dir, sizeof(dir), \"%s\", argv[2]);", "FAIL"),
            ("music-dir-word-limit", "{ CmdMusic(2, head); return; }", "{}", "FAIL"),
            ("music-type-unchecked", "for (int t = CE_MUSIC_SINGLE; argc == 3 && t <= CE_MUSIC_RANDOM; ++t)",
             "for (int t = CE_MUSIC_SINGLE; t <= CE_MUSIC_RANDOM; ++t)", "FAIL"),
        ]
        caught = []
        for name, old, new, report in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                folder = self.out / name
                self.fixture(folder, source.replace(old, new))
                ran = self.run_driver(self.build(folder, "ce", True))
                if ran.returncode == 0:
                    ran = self.run_driver(self.build(folder, "ce", True), "denied", {"BEA_PORT_CE_LAYER": "1", "BEA_PORT_TAPE": "1"})
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived")
                text = ran.stdout + ran.stderr
                if report == "out of bounds":	# UBSan's bounds check or ASan's buffer overflow, whichever stops it first
                    self.assertRegex(text, r"out of bounds|buffer-overflow", name)
                else:
                    self.assertIn(report, text, name)
                caught.append(name)
        self.assertGreaterEqual(len(caught), 20)

    def test_no_retail_console_dispatch(self):
        for path in (PALETTE, ACTIONS):
            code = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.DOTALL)
            for forbidden in ("ExecuteCommand", "Exec(", "HandleBind", "con_bind", "SetVariable", "MemStats", "DumpMem",
                              "DumpInterestingTextures", "system(", "fopen(", "popen(", "GetSample(", "CreateSample"):
                self.assertNotIn(forbidden, code, f"{path.name}: {forbidden}")
        palette = re.sub(r"//[^\n]*|/\*.*?\*/", "", PALETTE.read_text(), flags=re.DOTALL)
        self.assertNotIn("CONSOLE.", palette)
        for needed in ("CE_BootRequested()", "PortRecording()", "kCheatCamera", "kCheatLevels"):
            self.assertIn(needed, palette)
        actions = ACTIONS.read_text()
        # The game's own command bodies, called directly: CConsole is only read (scrollback, variable lookup) and printed to.
        for call in ("LT.ShowGFXCardInfo()", "::ListAtmospherics(NULL)", "ENGINE.LandscapeNavDisplay(", "ENGINE.SetGammaBias(",
                     "GAME.DeclareLevelWon()", "GAME.DeclareLevelLost()", "FMV.PlayFullscreen(", "SOUND.PlayNamedSample(",
                     "CONSOLE.FindVariable(", 'CONSOLE.Print("%s\\n", line)', "SOUND.GetFirstSample()"):
            self.assertIn(call, actions)
        # The game's films only, never a typed path.
        film = function(actions, "bool Film(")
        self.assertIn("MakeCutsceneName(made, number)", film)
        self.assertIn("number > 33", film)
        cheat = (ROOT / "port/game/ce_frontend.cpp").read_text()
        self.assertIn("console", cheat.lower())

    def test_patched_callers(self):
        files = patched(self.out, ["game.cpp", "System.cpp", "console.cpp", "ltshell.cpp", "DXEngine.cpp"])
        game = files["game.cpp"]
        shutdown = function(game, "void\tCGame::ShutdownRestartLoop()")
        self.assertLess(shutdown.index("PortCePaletteClose()"), shutdown.index("PortCeConsoleLevelEnd()"))
        self.assertLess(shutdown.index("PortCeConsoleLevelEnd()"), shutdown.index("SAFE_DELETE(mController"))
        self.assertLess(shutdown.index("PortCeConsoleLevelEnd()"), shutdown.index("WORLD.Shutdown()"))
        init = function(game, "BOOL CGame::InitRestartLoop()")
        self.assertLess(init.index("mHorizontalSplitscreen=true"), init.index("PortCeConsoleLevelStart()"))
        self.assertLess(init.index('"cg_horizontalsplitscreen"'), init.index("PortCeConsoleLevelStart()"))
        loop = function(game, "void\tCGame::MainLoop()")
        self.assertLess(loop.rindex("if (qt != QT_NONE)"), loop.index("PortCeConsoleFrame()"))
        self.assertLess(loop.index("PortCeConsoleFrame()"), loop.index("Update();"))
        script = re.search(r"#ifdef CE_VERSION\n(?:\s*//[^\n]*\n)*\s*if \(!PortCeLayer\(\)\)\n#endif\n\s*CONSOLE\.Exec\(\"autoexec\.con\"\);", game)
        self.assertIsNotNone(script, "autoexec.con runs only with the CE layer off")
        shell = function(files["ltshell.cpp"], "LRESULT PCLTShell::MsgProc(")
        self.assertEqual(len(re.findall(r"if \(!PortCeLayer\(\)\)[^\n]*\n#endif\n\s*CONSOLE\.HandleBind", shell)), 2)
        system = files["System.cpp"]
        run = function(system, "SINT\tCSystem::Run()")
        jump = run[run.index("GAME.RunLevel(result)"):]
        self.assertIn("for (SINT next = PortCeConsoleTakeLevel(); quit == QT_QUIT_TO_FRONTEND && next > 0;", jump)
        self.assertLess(jump.index("FRONTEND.mSelectedLevel = next;"), jump.index("quit = GAME.RunLevel(next);"))
        self.assertLess(jump.index("quit = GAME.RunLevel(next);"), jump.index("if (quit == QT_QUIT_TIMEOUT)"))
        start = game.index("void CGame::DrawDebugStuff()")
        debug = game[start:game.index("LOG.Render();", start)]	# its #if branches do not balance braces
        block = debug[debug.index("if (PortCeDevStatsOn())"):debug.index("PortCeDevStatsDraw();")]
        retail = debug[debug.index("#else\n#ifdef CE_VERSION"):]
        self.assertLess(retail.index("PortCeDevStatsOn()"), retail.index("gShowUnfinishedWatermark"))
        # Read only: no assignment, increment or call that is not a getter, the font or the overlay's own buffer.
        code = re.sub(r'"[^"]*"', '""', block.replace("CBattleEngine* player_unit = p->GetBattleEngine()", "").replace("CPlayer* p = GetPlayer(0) ;", ""))
        self.assertIsNone(re.search(r"[^=!<>]=[^=]|\+\+|--", code))
        self.assertNotIn("TEXDATA", block)
        self.assertIn("DEFAULT Heap - Size", block)
        engine = function(files["DXEngine.cpp"], "HRESULT\tCDXEngine::PostRender(")
        self.assertLess(engine.index("GAMEINTERFACE.Render()"), engine.index("PortCePaletteRender()"))
        # The bounded print: run the patched CConsole::Print with the CE layer on and off.
        printer = function(files["console.cpp"], "void CConsole::Print(char *fmt, ...)")
        (self.out / "print.inc").write_text(printer)
        (self.out / "print_driver.cpp").write_text(PRINT_DRIVER)
        for ce in (True, False):
            exe = self.out / ("print-ce" if ce else "print-faithful")
            cmd = [os.environ.get("CXX", "g++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-Wno-format-security",
                   *(["-DCE_VERSION=1"] if ce else []), "-I", str(self.out), str(self.out / "print_driver.cpp"), "-o", str(exe)]
            built = subprocess.run(cmd, capture_output=True, text=True, timeout=60, preexec_fn=no_core)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("bounded" if ce else "retail", ran.stdout)
        self.assertRegex(printer, r"if \(bounded\)\s*vsnprintf\(text, sizeof\(text\), fmt, args\);\s*else\s*#endif\s*vsprintf\(text, fmt, args\);")
        self.assertRegex(printer, r"if \(bounded && pos >= 0x7f\)\s*continue;")


PRINT_DRIVER = r'''
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CONSOLE_LINES 60
#define TRACE(x) (void)(x)
struct { bool mResBuilderMode = false; } CLIPARAMS;
struct { void SetRegKey(const char *, const char *) {} } PLATFORM;
static bool layer = true;
bool PortCeLayer() { return layer; }
struct CConsole {
    bool mInitialised = true; int mTrace = 0;
    char mLines[CONSOLE_LINES][0x80];
    char mInputLine[0x80];
    void Print(char *fmt, ...);
};
#include "print.inc"
static void check(bool c, const char *m) { if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); std::exit(2); } }
int main() {
    static CConsole con;
    std::memset(con.mLines, 0, sizeof con.mLines);
    std::strcpy(con.mLines[3], "keep");
    std::strcpy(con.mInputLine, "input");
    char longline[400];
    std::memset(longline, 'x', 300);
    std::strcpy(longline + 300, "\n");
#ifdef CE_VERSION
    con.Print((char *)"%s", longline);
    check(std::strlen(con.mLines[1]) == 0x7f, "a long line stops at its row");
    check(!std::strcmp(con.mLines[4], "keep") && !std::strcmp(con.mInputLine, "input"), "no other row or the input line is written");
    char huge[0x3000];
    std::memset(huge, 'y', sizeof huge - 1); huge[sizeof huge - 1] = 0;
    con.Print((char *)"%s%s\n", huge, huge);
    check(std::strlen(con.mLines[1]) == 0x7f && !std::strcmp(con.mInputLine, "input"), "text past the print buffer is cut, not copied");
    std::puts("bounded print passed");
#else
    con.Print((char *)"%s %d\n", "short", 7);
    check(!std::strcmp(con.mLines[1], "short 7") && !std::strcmp(con.mLines[4], "keep"), "the faithful build prints as retail");
    std::puts("retail print kept");
#endif
}
'''


if __name__ == "__main__":
    unittest.main(verbosity=2)
