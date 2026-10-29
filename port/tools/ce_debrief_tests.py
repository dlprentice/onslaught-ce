#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the CE end-of-level statistics panel: the block patch 0104 adds to CGame's end screen (taken from game.cpp after
every port patch), the actual ce_debrief.cpp and ce.cpp's ce.ini reader, against recording fakes of the fonts, the text
database and the player; no game, renderer or retail data. Rows, formats, positions, the CHEATER! line, the screen
bounds, ce.ini's debrief_stats, the fences and the proof switch. Causal mutants must each be caught.
"""
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402
TESTS = ROOT / "port/tools/tests"
DEBRIEF = ROOT / "port/game/ce_debrief.cpp"
PATCH = ROOT / "port/patches/0104-ce-debrief-stats.patch"
FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT",
          "BEA_NET_HOST", "BEA_NET_JOIN")
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


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


def patched_game(out, patch_text=None):
    """The mirror's game.cpp with every port patch's game.cpp section applied in the build's order, at fuzz 0."""
    (out / "game.cpp").write_bytes(mirror_text("game.cpp"))
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        text = patch_text if patch == PATCH and patch_text is not None else patch.read_text(encoding="latin1")
        for m in re.finditer(r"(?ms)^--- a/game\.cpp\n\+\+\+ b/game\.cpp\n.*?(?=^--- a/|\Z)", text):
            (out / "section.patch").write_text(m.group(0), encoding="latin1")
            r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                               capture_output=True, text=True, timeout=10, preexec_fn=no_core)
            if r.returncode or "fuzz" in r.stdout.lower():
                raise AssertionError(f"{patch.name}: {r.stdout}{r.stderr}")
    return (out / "game.cpp").read_text(encoding="latin1")


def panel_block(game):
    start = game.index("#ifdef CE_VERSION\n\t\t\t\t// Patch 0104")
    end = game.index("#endif", start)
    return game[start:end + len("#endif")] + "\n"


class DebriefTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-debrief-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, debrief=None, patch_text=None, ce=True, network=False):
        folder = self.out / name
        game = folder / "game"
        common = folder / "platform/common"
        game.mkdir(parents=True)
        common.mkdir(parents=True)
        (folder / "updater").mkdir()
        block = panel_block(patched_game(folder, patch_text))
        self.assertIn("PortCeDebriefStatsOn(", block)
        (game / "panel.inc").write_text(block)
        for n in ("common.h", "CLIParams.h"):
            (game / n).write_text('#include "ce_debrief_fake.h"\n')
        shutil.copy(TESTS / "ce_debrief_fake.h", game)
        shutil.copy(ROOT / "port/game/ce_debrief.h", game)
        (game / "ce_debrief.cpp").write_text(debrief if debrief is not None else DEBRIEF.read_text())
        for n in ("portlog.h", "ce.cpp"):
            shutil.copy(ROOT / "port/platform/common" / n, common)
        shutil.copy(ROOT / "port/updater/ce_report.h", folder / "updater")
        driver = (TESTS / "ce_debrief_driver.cpp").read_text().replace("PANEL_INC", str(game / "panel.inc"))
        (game / "driver.cpp").write_text(driver)
        cxx = os.environ.get("CXX", "g++")
        defines = ['-DCE_VERSION="test"'] if ce else []
        objects = []
        units = [(game / "driver.cpp", ["-Wall", "-Werror", "-Wno-format-truncation", "-Wno-write-strings", "-include", "ce_debrief.h"]),
                 (game / "ce_debrief.cpp", ["-Wall", "-Wextra", "-Werror"]), (common / "ce.cpp", ["-w"])]
        if network:
            (game / "net.cpp").write_text('#include <stdlib.h>\nextern "C" int CE_BootRequested(void) { return getenv("TEST_NET_BOOT") != 0; }\n')
            units.append((game / "net.cpp", []))
        for source, flags in units:
            obj = folder / (source.stem + ".o")
            r = subprocess.run([cxx, "-std=c++17", "-g", *SANITIZE, *defines, *flags, "-I", str(game), "-c", str(source),
                                "-o", str(obj)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)
            self.assertEqual(r.returncode, 0, f"{source.name}: {r.stderr}")
            objects.append(str(obj))
        exe = folder / "driver"
        r = subprocess.run([cxx, *SANITIZE, *objects, "-o", str(exe)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        self.assertEqual(r.returncode, 0, r.stderr)
        return exe

    def run_case(self, exe, mode, ini="", extra=None):
        user = self.out / f"user-{len(list(self.out.glob('user-*')))}"
        user.mkdir()
        (user / "ce.ini").write_text(ini)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "TEST_"))}
        env.update(BEA_USER_DIR=str(user), ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        env.update(extra or {})
        return subprocess.run([str(exe), mode], env=env, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def passes(self, ran, text):
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn(text, ran.stdout)

    def test_panel(self):
        exe = self.build("actual")
        self.passes(self.run_case(exe, "rows"), "panel rows passed")
        self.passes(self.run_case(exe, "rows", "debrief_stats=1\n"), "panel rows passed")
        self.passes(self.run_case(exe, "off", "debrief_stats=0\n"), "panel off")
        for flag in FENCES:
            with self.subTest(flag=flag):
                self.passes(self.run_case(exe, "off", extra={flag: "1"}), "panel off")
                self.passes(self.run_case(exe, "off", extra={flag: "1", "BEA_PORT_CE_LAYER": "1"}), "panel off")
        for flag in ("TEST_RECORDING", "TEST_ATTRACT", "TEST_DEMO", "TEST_MULTI"):
            self.passes(self.run_case(exe, "off", extra={flag: "1"}), "panel off")
        self.passes(self.run_case(exe, "off", extra={"BEA_PORT_CE_LAYER": "0"}), "panel off")
        # The proof switch draws it under a tape only with the CE layer forced on; ce.ini can still turn it off.
        self.passes(self.run_case(exe, "off", extra={"BEA_PORT_TAPE": "t", "BEA_PORT_DEBRIEF_PROOF": "1"}), "panel off")
        self.passes(self.run_case(exe, "rows", extra={"BEA_PORT_TAPE": "t", "BEA_PORT_DEBRIEF_PROOF": "1",
                                                      "BEA_PORT_CE_LAYER": "1"}), "panel rows passed")
        net = self.build("network", network=True)
        self.passes(self.run_case(net, "off", extra={"TEST_NET_BOOT": "1"}), "panel off")
        self.passes(self.run_case(net, "off", extra={"TEST_NET_BOOT": "1", "BEA_PORT_TAPE": "t", "BEA_PORT_DEBRIEF_PROOF": "1",
                                                     "BEA_PORT_CE_LAYER": "1"}), "panel off")

    def test_faithful_build_has_no_panel(self):
        game = patched_game(self.out)
        start = game.index("case 5:\tstr = TEXT_DB.GetString(DAMAGE_TAKEN);")
        end = game.index("gInFrontEnd = false;", start)
        outside = re.sub(r"#ifdef CE_VERSION.*?#endif", "", game[start:end], flags=re.DOTALL)
        code = re.sub(r"//[^\n]*|/\*.*?\*/", "", outside, flags=re.DOTALL)
        self.assertNotIn("DrawTextShadowed", code, "only the CE block draws the rows")

    def test_mutants_are_caught(self):
        source = DEBRIEF.read_text()
        patch = PATCH.read_text()
        mutants = [
            ("no-tape-fence", "debrief", '"BEA_PORT_TAPE", "BEA_PORT_PROBES"', '"BEA_UNUSED", "BEA_PORT_PROBES"',
             ("off", "", {"BEA_PORT_TAPE": "1", "BEA_PORT_CE_LAYER": "1"})),
            ("no-recording-fence", "debrief", "if (PortRecording()) return false;", "", ("off", "", {"TEST_RECORDING": "1"})),
            ("multiplayer", "debrief", "if (!PortCeLayer() || !singlePlayer) return false;", "if (!PortCeLayer() || (!singlePlayer && false)) return false;",
             ("off", "", {"TEST_MULTI": "1"})),
            ("default-off", "debrief", 'CE_SettingGetBool("debrief_stats", 1)', 'CE_SettingGetBool("debrief_stats", 0)', ("rows", "", None)),
            ("ini-ignored", "debrief", "return ini != 0;", "return true;", ("off", "debrief_stats=0\n", None)),
            ("no-bottom", "debrief", "if (panel.y + tall <= panel.bottom || rows < 2)", "if (tall >= 0.f || rows < 2)", ("rows", "", None)),
            ("no-rise", "debrief", "if (over > 0.f && over <= panel.rise) panel.y -= over;", "(void)over;", ("rows", "", None)),
            ("rise-unbounded", "debrief", "if (over > 0.f && over <= panel.rise) panel.y -= over;", "if (over > 0.f) panel.y -= over;",
             ("rows", "", None)),
            ("one-column-only", "debrief", "const bool sideBySide = second + width <= panel.right;", "const bool sideBySide = false;",
             ("rows", "", None)),
            ("accuracy-always", "patch", "+\t\t\t\t\tif (p->GetStat(PS_ROUNDSFIRED)>0)\n", "+\t\t\t\t\tif (true)\n", ("rows", "", None)),
            ("value-column", "patch", "+\t\t\t\t\t\tPLATFORM.Font( FONT_SMALL )->DrawTextShadowed(ceX[ceRow]+nc,",
             "+\t\t\t\t\t\tPLATFORM.Font( FONT_SMALL )->DrawTextShadowed(ceX[ceRow]+cc,", ("rows", "", None)),
            ("cheater-invisible", "patch", "0xffff0000,ToWCHAR(\"CHEATER!\")", "0x00FF0000,ToWCHAR(\"CHEATER!\")", ("rows", "", None)),
            ("god-not-counted", "patch", "PS_CHEATED)>0 || p->IsGod();", "PS_CHEATED)>0;", ("rows", "", None)),
            ("no-player-check", "patch", "+\t\t\t\tif (p && PortCeDebriefStatsOn(", "+\t\t\t\tif (PortCeDebriefStatsOn(", ("rows", "", None)),
        ]
        caught = []
        for name, where, old, new, case in mutants:
            with self.subTest(mutant=name):
                text = source if where == "debrief" else patch
                self.assertEqual(text.count(old), 1, name)
                exe = self.build(name, debrief=source.replace(old, new) if where == "debrief" else None,
                                 patch_text=patch.replace(old, new) if where == "patch" else None)
                ran = self.run_case(exe, *case)
                self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived: {ran.stdout}")
                caught.append(name)
        self.assertEqual(len(caught), len(mutants))


if __name__ == "__main__":
    unittest.main(verbosity=2)
