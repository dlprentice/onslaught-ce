#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Playable Multi-Battle-Engine (2v2 Versus & 3-4 BE Skirmish/Co-op) + Play Side test suite.

    python3 -B port/tools/ce_multibe_tests.py [-v]

Verifies:
  1. Host helper logic & causal mutants (port/game/ce_multibe.h + port/game/ce_multibe_state.h):
     - opt-in config & menu-choice <-> ce.ini roundtrip (`play_side`, `multi_be`), failing closed on off/invalid;
     - 4-slot team-aware spawn inversion (`1,3 <-> 2,4`) without double-swapping CLI 0359 or retail mInvertSides;
     - CE-side P3/P4 career preference storage (keeping retail CCareer [2] .bes save layout unchanged);
     - Walker <-> Jet morph decisions (strict never-land-in-water invariant!), Jet flight control clearance,
       3-BE / 4-BE formation separation, finite-position / NaN guard, panoramic overview camera framing,
       respawn timers, and 2v2 / 4-BE HUD banner formatting.
  2. Gate, neutrality & patch-linkage static invariants:
     - zero BEA_* env vars in ce_multibe.h, ce_multibe_state.h, ce_multibe.cpp;
     - CE_GATE_LAYER gating on every Multi-BE gameplay/render/menu hook row in ce_hooks.cpp;
     - network/lockstep guard, stack-local ListIterator<CUnit>, water safety, split-screen hud_focus safety,
       and strong definitions for all Phase 1 patch weak symbols.
  3. Live engine proof (real compiled Linux `bea` binary, 100% offscreen):
     - Default run (`multi_be` unset): 0 `ce-multibe:` lines emitted, clean exit (zero 1P/2P regression).
     - Replay fence (`BEA_PORT_CE_LAYER` unset with virtual clock): 0 `ce-multibe:` lines even when `ce.ini` enables it.
     - 2v2 Versus (Level 850, `multi_be=4`, `play_side=forseti`): 4 Battle Engines, >=200 ticks, Walker+Jet morphs,
       auxiliary weapon fire, AI targeting, movement, 0 NaNs, 0 crashes.
     - 2v2 Versus as Muspell (Level 850, `multi_be=4`, `play_side=muspell`): Player 1 spawns as Muspell (`side=muspell`),
       4 Battle Engines active & fighting, >=200 ticks, 0 NaNs.
     - 4-BE Allied Campaign/Co-op (Level 110 & Level 100): 4 Battle Engines, >=200 ticks, firing & moving, 0 NaNs.
     - 3-BE variant (Level 110, `multi_be=3`): 3 Battle Engines, >=200 ticks, firing, 0 NaNs.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SCRATCH = ROOT / "local-data/port/ce-multibe-tests"
DEFAULT_EXE = ROOT / "local-data/port/build/linux/bea"

HOST_DRIVER = r"""
#include "ce_multibe.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static int gFail = 0;
static void Check(bool ok, const char *what) {
    if (!ok) { std::printf("FAIL: %s\n", what); gFail++; }
}

int main() {
    // 1. Config & menu-choice parsing fails closed on absent/off/invalid values
    Check(CeMultiBeParseCount(nullptr) == 0, "null -> 0");
    Check(CeMultiBeParseCount("") == 0, "empty -> 0");
    Check(CeMultiBeParseCount("0") == 0, "0 -> 0");
    Check(CeMultiBeParseCount("false") == 0, "false -> 0");
    Check(CeMultiBeParseCount("off") == 0, "off -> 0");
    Check(CeMultiBeParseCount("2") == 0, "2 -> 0 (standard)");
    Check(CeMultiBeParseCount("-1") == 0, "-1 -> 0");
    Check(CeMultiBeParseCount("99") == 0, "99 -> 0");
    Check(CeMultiBeParseCount("bogus") == 0, "bogus -> 0");
    Check(CeMultiBeParseCount("1") == 4, "1 -> 4");
    Check(CeMultiBeParseCount("true") == 4, "true -> 4");
    Check(CeMultiBeParseCount("ON") == 4, "ON -> 4");
    Check(CeMultiBeParseCount("3") == 3, "3 -> 3");
    Check(CeMultiBeParseCount("4") == 4, "4 -> 4");

    Check(CeMultiBeParsePlaySide(nullptr) == CE_MULTIBE_SIDE_FORSETI, "null side -> forseti");
    Check(CeMultiBeParsePlaySide("forseti") == CE_MULTIBE_SIDE_FORSETI, "forseti");
    Check(CeMultiBeParsePlaySide("Muspell") == CE_MULTIBE_SIDE_MUSPELL, "Muspell case-insensitive");
    Check(std::strcmp(CeMultiBePlaySideIniValue(CE_MULTIBE_SIDE_FORSETI), "forseti") == 0, "ini forseti");
    Check(std::strcmp(CeMultiBePlaySideIniValue(CE_MULTIBE_SIDE_MUSPELL), "muspell") == 0, "ini muspell");

    Check(CeMultiBeModeFromIni("2") == CE_MULTIBE_CHOICE_STANDARD, "mode 2 -> standard");
    Check(CeMultiBeModeFromIni("3") == CE_MULTIBE_CHOICE_THREE, "mode 3 -> three");
    Check(CeMultiBeModeFromIni("4") == CE_MULTIBE_CHOICE_FOUR, "mode 4 -> four");
    Check(std::strcmp(CeMultiBeModeToIni(CE_MULTIBE_CHOICE_STANDARD), "2") == 0, "standard -> 2");
    Check(std::strcmp(CeMultiBeModeToIni(CE_MULTIBE_CHOICE_THREE), "3") == 0, "three -> 3");
    Check(std::strcmp(CeMultiBeModeToIni(CE_MULTIBE_CHOICE_FOUR), "4") == 0, "four -> 4");

    Check(CeMultiBeParseHudSlot(nullptr) == -1, "hud null -> -1");
    Check(CeMultiBeParseHudSlot("") == -1, "hud empty -> -1");
    Check(CeMultiBeParseHudSlot("0") == 0, "hud 0 -> 0");
    Check(CeMultiBeParseHudSlot("3") == 3, "hud 3 -> 3");
    Check(CeMultiBeParseHudSlot("4") == -1, "hud 4 -> -1");
    Check(CeMultiBeParseHudSlot("-1") == -1, "hud -1 -> -1");

    // 2. 4-slot spawn inversion & config selection (zero double-swap with 0359 or retail mInvertSides)
    Check(CeMultiBeTransformStartSlot(1, false, false, false, true) == 1, "default P1 stays 1");
    Check(CeMultiBeTransformStartSlot(1, false, false, true, true) == 2, "menu play_side=muspell swaps P1->2 on Versus");
    Check(CeMultiBeTransformStartSlot(2, false, false, true, true) == 1, "menu play_side=muspell swaps P2->1 on Versus");
    Check(CeMultiBeTransformStartSlot(1, false, true, true, true) == 1, "no double-swap when CLI 0359 already inverted");
    Check(CeMultiBeTransformStartSlot(1, true, false, true, true) == 1, "no double-swap when retail mInvertSides already inverted");
    Check(CeMultiBeTransformStartSlot(1, false, false, true, false) == 1, "campaign map never inverts P1");
    Check(CeMultiBeTransformStartSlot(3, false, false, true, true) == 4, "menu play_side=muspell swaps P3->4 on Versus");
    Check(CeMultiBeTransformStartSlot(4, false, true, false, true) == 3, "CLI 0359 swaps P4->3 on Versus");
    Check(CeMultiBeTransformStartSlot(3, true, false, false, false) == 4, "retail mInvertSides swaps P3->4");

    Check(CeMultiBeSelectConfigId(1, 10, 20) == 10, "P1 config");
    Check(CeMultiBeSelectConfigId(2, 10, 20) == 20, "P2 config");
    Check(CeMultiBeSelectConfigId(3, 10, 20) == 10, "P3 defaults to P1 team config");
    Check(CeMultiBeSelectConfigId(4, 10, 20) == 20, "P4 defaults to P2 team config");
    Check(CeMultiBeSelectConfigId(3, 10, 20, 30, 40) == 30, "P3 explicit config");
    Check(CeMultiBeSelectConfigId(4, 10, 20, 30, 40) == 40, "P4 explicit config");

    // 3. CE-side P3/P4 career preference storage (bounds safe for slots 0..1 and >=4)
    CeMultiBeExtraSlotPrefs prefs;
    prefs.Reset();
    prefs.Set(2, CE_PREF_IS_GOD, 1);
    prefs.Set(2, CE_PREF_INVERT_Y, 1);
    prefs.Set(3, CE_PREF_INVERT_JET_Y, 1);
    prefs.Set(3, CE_PREF_VIBRATION, 1);
    prefs.Set(3, CE_PREF_CONTROLLER_CFG, 2);
    prefs.Set(0, CE_PREF_IS_GOD, 1);  // ignored (handled by retail CCareer)
    prefs.Set(99, CE_PREF_IS_GOD, 1); // out of range ignored safely
    Check(prefs.Get(2, CE_PREF_IS_GOD) == 1, "slot 2 god");
    Check(prefs.Get(2, CE_PREF_INVERT_Y) == 1, "slot 2 invertY");
    Check(prefs.Get(2, CE_PREF_VIBRATION) == 0, "slot 2 vib untouched");
    Check(prefs.Get(3, CE_PREF_INVERT_JET_Y) == 1, "slot 3 invertJetY");
    Check(prefs.Get(3, CE_PREF_VIBRATION) == 1, "slot 3 vib");
    Check(prefs.Get(3, CE_PREF_CONTROLLER_CFG) == 2, "slot 3 cfg");
    Check(prefs.Get(0, CE_PREF_IS_GOD) == 0, "slot 0 returns 0");
    Check(prefs.Get(99, CE_PREF_IS_GOD) == 0, "slot 99 returns 0");

    // 4. Walker <-> Jet morph decisions & strict never-land-in-water invariant
    CeMultiBeMorphInput mi = {};
    mi.isJet = true; mi.flightAllowed = true; mi.groundIsDry = false;
    mi.energyRatio = 0.05f; mi.targetDist = 15.0f; mi.ticksSinceMorph = 200;
    Check(!CeMultiBeShouldMorph(mi), "NEVER morph Jet -> Walker over water even at low energy");
    mi.groundIsDry = true;
    Check(CeMultiBeShouldMorph(mi), "morph Jet -> Walker over dry land at low energy");
    mi.isJet = false; mi.nearWaterHazard = true; mi.energyRatio = 0.5f; mi.ticksSinceMorph = 5;
    Check(CeMultiBeShouldMorph(mi), "Walker near water takes off immediately");
    mi.nearWaterHazard = false; mi.targetDist = 120.0f; mi.energyRatio = 0.7f; mi.ticksSinceMorph = 100;
    Check(CeMultiBeShouldMorph(mi), "Walker takes off for distant target");
    mi.ticksSinceMorph = 10;
    Check(!CeMultiBeShouldMorph(mi), "morph cooldown prevents thrashing");

    // 5. Jet flight control: emergency pull-up above ground/water & fire alignment
    CeMultiBeJetCommand lowCmd = CeMultiBeComputeJetControl(0.0f, 0.0f, -6.0f, -10.0f, -5.0f, 0.0f, 80.0f, 0.0f, true, 24.0f);
    Check(lowCmd.pitch > 0.2f, "jet pulls nose UP when too close to terrain/water");
    Check(lowCmd.thrust >= 0.99f, "jet applies full thrust during pull-up");
    Check(!lowCmd.fire, "jet suppresses fire during emergency pull-up");

    CeMultiBeJetCommand fireCmd = CeMultiBeComputeJetControl(0.0f, 0.0f, -35.0f, -10.0f, -5.0f, 2.0f, 80.0f, 0.0f, true, 24.0f);
    Check(fireCmd.fire, "jet fires when aligned on hostile at safe altitude");

    // 6. Match state: team kills, 100-tick respawn timer, HUD banner formatting
    CeMultiBeMatchState ms;
    ms.Reset();
    ms.RegisterSlotTeam(0, CE_MULTIBE_SIDE_FORSETI);
    ms.RegisterSlotTeam(1, CE_MULTIBE_SIDE_MUSPELL);
    ms.RegisterSlotTeam(2, CE_MULTIBE_SIDE_FORSETI);
    ms.RegisterSlotTeam(3, CE_MULTIBE_SIDE_MUSPELL);
    ms.OnAuxDestroyed(3);
    Check(ms.forsetiKills == 1 && ms.muspellKills == 0, "destroying Muspell BE credits Forseti kill");
    Check(ms.respawnTimer[3] == CeMultiBeMatchState::kRespawnTicks, "respawn timer armed");
    uint8_t ready = 0;
    for (int t = 0; t < CeMultiBeMatchState::kRespawnTicks - 1; ++t)
        ready |= ms.TickRespawnTimers();
    Check(ready == 0, "does not respawn early");
    ready = ms.TickRespawnTimers();
    Check(ready == (uint8_t)(1u << 3), "slot 3 respawns on tick 100");
    ms.forsetiAlive = 2; ms.forsetiTotal = 2; ms.muspellAlive = 1; ms.muspellTotal = 2;
    char banner[128] = {};
    ms.FormatHudBanner(true, CE_MULTIBE_SIDE_MUSPELL, banner, sizeof(banner));
    Check(std::strstr(banner, "2v2 VERSUS") != nullptr && std::strstr(banner, "MUSPELL") != nullptr, "2v2 banner text");
    ms.walkerCount = 2; ms.jetCount = 2;
    ms.FormatHudBanner(false, CE_MULTIBE_SIDE_FORSETI, banner, sizeof(banner));
    Check(std::strstr(banner, "4-BE SQUAD") != nullptr && std::strstr(banner, "JET 2") != nullptr, "4-BE squad banner text");

    // 7. Finite position guard & dry-land water guard
    const float nanv = std::numeric_limits<float>::quiet_NaN();
    const float infv = std::numeric_limits<float>::infinity();
    Check(CeMultiBeIsFinitePos(10.0f, -20.0f, 5.0f), "normal pos valid");
    Check(!CeMultiBeIsFinitePos(nanv, 0.0f, 0.0f), "NaN X rejected");
    Check(!CeMultiBeIsFinitePos(0.0f, nanv, 0.0f), "NaN Y rejected");
    Check(!CeMultiBeIsFinitePos(0.0f, 0.0f, infv), "Inf Z rejected");
    Check(!CeMultiBeIsFinitePos(200000.0f, 0.0f, 0.0f), "huge coord rejected");

    Check(CeMultiBeIsAboveWater(-15.0f, -5.0f, 2.0f), "dry hill above water passes");
    Check(!CeMultiBeIsAboveWater(-6.5f, -5.0f, 2.0f), "within 2m water margin rejected");
    Check(!CeMultiBeIsAboveWater(-5.0f, -5.0f, 2.0f), "at water level rejected");
    Check(!CeMultiBeIsAboveWater(2.0f, -5.0f, 2.0f), "underwater rejected");
    Check(!CeMultiBeIsAboveWater(nanv, -5.0f, 2.0f), "NaN ground rejected");

    // 8. Formation separation & overview camera framing
    for (int total = 3; total <= 4; total++) {
        float pts[4][2];
        for (int i = 0; i < total; i++) {
            CeMultiBeFormationOffset(i, total, 16.0f, &pts[i][0], &pts[i][1]);
            Check(CeMultiBeIsFinitePos(pts[i][0], pts[i][1], 0.0f), "formation offset finite");
        }
        for (int i = 0; i < total; i++) {
            for (int j = i + 1; j < total; j++) {
                float dx = pts[i][0] - pts[j][0];
                float dy = pts[i][1] - pts[j][1];
                Check(std::sqrt(dx * dx + dy * dy) >= 12.0f, "formation pairwise separation >= 12m");
            }
        }
    }
    float bePos[4][3] = {
        {100.0f, 200.0f, -10.0f},
        {84.0f,  210.0f, -10.0f},
        {116.0f, 210.0f, -10.0f},
        {100.0f, 224.0f, -10.0f},
    };
    float cam[3] = {0}, look[3] = {0};
    Check(CeMultiBeComputeOverviewCam(bePos, 4, 0.0f, 1.0f, 34.0f, 11.5f, cam, look), "overview cam succeeds");
    Check(cam[2] < look[2], "camera is above look-at target (-Z up)");
    Check(cam[1] < look[1], "camera sits behind formation along -fwd");
    float nanPos[1][3] = {{nanv, 0.0f, 0.0f}};
    Check(!CeMultiBeComputeOverviewCam(nanPos, 1, 0.0f, 1.0f, 34.0f, 11.5f, cam, look), "all-NaN positions rejected");

    if (!gFail) std::puts("multibe-host-ok");
    return gFail != 0;
}
"""


def parse_telemetry(stderr_text):
    """Returns list of dicts parsed from `ce-multibe: k=v ...` lines."""
    rows = []
    for line in stderr_text.splitlines():
        if "ce-multibe:" not in line:
            continue
        payload = line.split("ce-multibe:", 1)[1].strip()
        row = {}
        for token in payload.split():
            if "=" in token:
                k, v = token.split("=", 1)
                row[k] = int(v) if re.fullmatch(r"-?\d+", v) else v
        if row:
            rows.append(row)
    return rows


def resolve_runtime_libs():
    roots = []
    if os.environ.get("BEA_PORT_DATA"):
        roots.append(Path(os.environ["BEA_PORT_DATA"]))
    roots.append(ROOT / "local-data/port-deps-canonical")
    git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"], capture_output=True, text=True)
    if git.returncode == 0 and git.stdout.strip():
        roots.append((ROOT / git.stdout.strip()).resolve().parent / "local-data/port-deps-canonical")
    for root in roots:
        if (root / "dxvk-native/usr/lib32").is_dir():
            dirs = [root / "prefix-sniper/lib", root / "dxvk-native/usr/lib32", root / "ffmpeg-shared-sniper/linux/usr/lib"]
            return [str(d) for d in dirs if d.is_dir()]
    return []


def run_bea_offscreen(exe, game_dir, run_dir, level, quit_frames, ce_ini_text=None, ce_layer=True, shot_at=None):
    """Runs `bea` 100% offscreen with isolated user dir, returning CompletedProcess."""
    user_dir = run_dir / "user"
    for sub in (user_dir, run_dir / "cache", run_dir / "runtime"):
        sub.mkdir(parents=True, exist_ok=True)
    if ce_ini_text is not None:
        (user_dir / "ce.ini").write_text(ce_ini_text, encoding="utf-8")
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": str(user_dir),
        "SDL_VIDEO_DRIVER": "offscreen",
        "BEA_PORT_REQUIRE_OFFSCREEN": "1",
        "SDL_AUDIO_DRIVER": "dummy",
        "BEA_PORT_REQUIRE_DUMMY_AUDIO": "1",
        "BEA_REPORT_NO_OPEN": "1",
        "BROWSER": "/bin/true",
        "DXVK_WSI_DRIVER": "SDL3",
        "DXVK_LOG_PATH": str(run_dir),
        "XDG_CACHE_HOME": str(run_dir / "cache"),
        "XDG_RUNTIME_DIR": str(run_dir / "runtime"),
        "BEA_USER_DIR": str(user_dir),
        "BEA_GAME_DIR": str(game_dir),
        "BEA_NO_UPDATE_CHECK": "1",
        "BEA_PORT_CLOCK": "fixed",
        "BEA_PORT_CLOCK_DT": "0.05",
        "BEA_PORT_QUIT": str(quit_frames),
    }
    if ce_layer:
        env["BEA_PORT_CE_LAYER"] = "1"
    if shot_at is not None:
        env["BEA_PORT_SHOT"] = f"{shot_at[0]}:{shot_at[1]}"
    libs = resolve_runtime_libs()
    if libs:
        env["LD_LIBRARY_PATH"] = ":".join(libs)
    cmd = [str(exe), "-level", str(level), "-skipfmv", "-forcewindowed", "-res", "1280", "720"]
    return subprocess.run(cmd, cwd=str(game_dir), env=env, capture_output=True, text=True, timeout=180)


class HostHelperTests(unittest.TestCase):
    def compile_and_run(self, header_text, state_text, tag, sanitize=True):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        safe_tag = tag.replace(" ", "-").replace("/", "-")
        base = Path(tempfile.mkdtemp(prefix=f"multibe-{safe_tag}-", dir=SCRATCH))
        try:
            (base / "ce_multibe.h").write_text(header_text, encoding="utf-8")
            (base / "ce_multibe_state.h").write_text(state_text, encoding="utf-8")
            driver = base / "driver.cpp"
            driver.write_text(HOST_DRIVER, encoding="utf-8")
            exe = base / "driver"
            cmd = [shutil.which("g++") or "c++", "-std=gnu++14", "-Wall", "-Werror", "-I", str(base), str(driver), "-o", str(exe)]
            if sanitize:
                cmd.insert(2, "-fsanitize=address,undefined")
                cmd.insert(3, "-fno-omit-frame-pointer")
            cc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if cc.returncode != 0:
                return cc.returncode, cc.stdout + cc.stderr
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            return run.returncode, run.stdout + run.stderr
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_host_helpers_pass_asan_ubsan(self):
        rc, out = self.compile_and_run(
            (PORT / "game/ce_multibe.h").read_text(encoding="utf-8"),
            (PORT / "game/ce_multibe_state.h").read_text(encoding="utf-8"),
            "clean",
            sanitize=True,
        )
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), "multibe-host-ok")

    def test_causal_mutants_are_caught(self):
        h_src = (PORT / "game/ce_multibe.h").read_text(encoding="utf-8")
        s_src = (PORT / "game/ce_multibe_state.h").read_text(encoding="utf-8")
        h_mutants = {
            "inverted Z elevation": ("outCamPos[2] = cz - heightAbove;", "outCamPos[2] = cz + heightAbove;"),
            "missing NaN check": (
                "if (!isfinite(x) || !isfinite(y) || !isfinite(z))\n\t\treturn false;\n\tconst float limit = 100000.0f;\n\treturn fabsf(x) < limit && fabsf(y) < limit && fabsf(z) < limit;",
                "return true;",
            ),
            "inverted water check": ("return groundZ < waterZ - margin;", "return groundZ > waterZ - margin;"),
            "zero-separation formation": ("*outRight = -spacing * 1.15f; *outForward = spacing * 0.45f;", "*outRight = 0.0f; *outForward = 0.0f;"),
            "parse accepts 2": ('if (CeMultiBeStrEqCase(text, "3"))\n\t\treturn 3;', 'if (CeMultiBeStrEqCase(text, "3") || CeMultiBeStrEqCase(text, "2"))\n\t\treturn 3;'),
        }
        for name, (old, new) in h_mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, h_src, name)
                rc, _ = self.compile_and_run(h_src.replace(old, new, 1), s_src, name.replace(" ", "-"), sanitize=False)
                self.assertNotEqual(rc, 0, f"mutant {name} was not caught")

        s_mutants = {
            "jet lands over water": ("if (!in.groundIsDry)\n\t\t\treturn false;", "if (false)\n\t\t\treturn false;"),
            "double-swaps CLI 0359": (
                "if (isVersusMap && menuPlaySideMuspell && !retailInvert && !cli0359Invert)",
                "if (isVersusMap && menuPlaySideMuspell)",
            ),
            "slot 3/4 never invert": (
                "const bool invert34 = retailInvert || (isVersusMap && (cli0359Invert || menuPlaySideMuspell));",
                "const bool invert34 = false;",
            ),
            "jet pulls down near ground": ("desiredPitch = 0.35f;", "desiredPitch = -0.35f;"),
            "wrong team kill credit": (
                "if (slotTeam[slot] == CE_MULTIBE_SIDE_MUSPELL)\n\t\t\tforsetiKills++;",
                "if (slotTeam[slot] == CE_MULTIBE_SIDE_FORSETI)\n\t\t\tforsetiKills++;",
            ),
        }
        for name, (old, new) in s_mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, s_src, name)
                rc, _ = self.compile_and_run(h_src, s_src.replace(old, new, 1), name.replace(" ", "-"), sanitize=False)
                self.assertNotEqual(rc, 0, f"mutant {name} was not caught")


class GateAuditTests(unittest.TestCase):
    def test_no_new_bea_env_vars_and_spdx_present(self):
        for rel in ("game/ce_multibe.h", "game/ce_multibe_state.h", "game/ce_multibe.cpp"):
            text = (PORT / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200], rel)
            self.assertIsNone(re.search(r"BEA_[A-Z0-9_]+", text), f"{rel} must not reference BEA_* env vars")

    def test_hooks_are_layer_gated(self):
        hooks = (PORT / "game/ce_hooks.cpp").read_text(encoding="utf-8")
        for row_name in ("multibe_level_begin", "multibe_tick_pre", "multibe_camera",
                         "multibe_hud_focus", "multibe_draw_hud", "multibe_menu_rows",
                         "multibe_unit_spawn", "multibe_unit_destroyed", "multibe_weapon_fire"):
            m = re.search(r'\{\s*\d+\s*,\s*"' + row_name + r'"\s*,\s*CE_HOOK_\w+\s*,\s*CE_GATE_(\w+)', hooks)
            self.assertIsNotNone(m, f"missing hook row {row_name}")
            self.assertEqual(m.group(1), "LAYER", f"{row_name} must use CE_GATE_LAYER")

    def test_network_guard_stack_iterators_water_death_and_menu_invariants(self):
        src = (PORT / "game/ce_multibe.cpp").read_text(encoding="utf-8")
        for sym in ("CE_BootRequested", "CE_SessionActive", "CE_LockstepRequested", "CE_LockstepSharing", "PortCeNetActive"):
            self.assertIn(sym, src, f"missing weak network/lockstep guard symbol {sym}")
        self.assertIn("if (NetworkOrLockstepActive())", src, "PortCeMultiBeLevelBegin must gate on NetworkOrLockstepActive()")
        self.assertNotIn("GetUnitNB().First()", src, "must not call WORLD.GetUnitNB().First(); use ListIterator<CUnit>")
        self.assertNotIn("GetUnitNB().Next()", src, "must not call WORLD.GetUnitNB().Next(); use ListIterator<CUnit>")
        self.assertGreaterEqual(src.count("ListIterator<CUnit>"), 3, "all three unit scans must use stack-local ListIterator<CUnit>")
        self.assertIn("MAP.GetWaterLevel()", src, "must check MAP.GetWaterLevel() when spawning/patrolling aux BEs")
        self.assertIn("PortCeMultiBeHandleDeath", src, "must intercept aux BE StartDieProcess cleanly")
        self.assertIn("focused->GetPlayer()->GetNumber() == player->GetNumber()", src, "2P split-screen hud_focus must match player numbers")
        self.assertIn("PortRecording()", src, "must guard start slot and side inversion against PortRecording()")
        self.assertIn("PortCeMultiBeAllowTeamKills", src, "must provide CE-layer gate for Muspell ConfirmedKill")
        self.assertIn('L"Play side: "', src, "clickable Play side menu row missing")
        self.assertIn('L"Battle Engines: "', src, "clickable Battle Engines menu row missing")


class LiveEngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ.get("BEA_EXE", str(DEFAULT_EXE)))
        cls.game_dir = Path(os.environ.get("BEA_GAME_DIR", ""))
        if not cls.exe.is_file():
            raise AssertionError(f"built Linux binary not found at {cls.exe} (build with port/build.py --target linux --public)")
        if not cls.game_dir.is_dir():
            raise AssertionError("BEA_GAME_DIR is not set to a valid game directory")

    def test_default_off_has_zero_multibe_activity(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-off-", dir=SCRATCH))
        try:
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=110, quit_frames=520, ce_ini_text="")
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            rows = parse_telemetry(proc.stderr)
            self.assertEqual(rows, [], f"expected zero ce-multibe telemetry when off, got {rows}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_replay_fence_disables_multibe_even_if_ini_enabled(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-fence-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\nmulti_be_cam=1\nplay_side=muspell\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=110, quit_frames=520, ce_ini_text=ini, ce_layer=False)
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            rows = parse_telemetry(proc.stderr)
            self.assertEqual(rows, [], f"CE_GATE_LAYER must suppress ce-multibe under virtual clock: {rows}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_four_battle_engines_2v2_versus_live_for_200_plus_ticks(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-850-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\nmulti_be_cam=1\nmulti_be_hud_slot=2\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=850, quit_frames=650, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 2, f"expected spawn + tick200/end telemetry, stderr tail:\n{proc.stderr[-1500:]}")
            final = rows[-1]
            self.assertEqual(final.get("target"), 4, final)
            self.assertGreaterEqual(final.get("active", 0), 3, f"expected active 2v2 Battle Engines: {final}")
            self.assertEqual(final.get("spawned"), 2, f"expected 2 auxiliary BEs added to 2P Versus: {final}")
            self.assertGreaterEqual(final.get("ticks", 0), 200, f"expected >=200 sim ticks: {final}")
            self.assertEqual(final.get("nan"), 0, f"NaN position detected: {final}")
            self.assertGreater(final.get("aux_shots", 0), 0, f"expected auxiliary BE weapon fire: {final}")
            self.assertGreater(final.get("ai_targeted", 0), 0, f"expected enemy AI targeting auxiliary BEs: {final}")
            self.assertGreater(final.get("hud_focus", 0), 0, f"expected hud_focus hook invocations: {final}")
            self.assertGreaterEqual(final.get("moved", 0), 2, f"expected auxiliary BEs to move cleanly: {final}")
            self.assertGreater(final.get("morphs", 0), 0, f"expected Walker<->Jet morphing in 2v2 combat: {final}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_four_battle_engines_2v2_muspell_side_live(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-851-muspell-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\nmulti_be_cam=1\nplay_side=muspell\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=851, quit_frames=620, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 2, proc.stderr[-1500:])
            final = rows[-1]
            self.assertEqual(final.get("side"), "muspell", f"expected Player 1 on Muspell side on Versus L851: {final}")
            self.assertEqual(final.get("target"), 4, final)
            self.assertEqual(final.get("spawned"), 2, final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertGreater(final.get("aux_shots", 0), 0, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_four_battle_engines_campaign_live_for_200_plus_ticks(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-110-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\nmulti_be_cam=1\nmulti_be_hud_slot=2\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=110, quit_frames=600, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 2, f"expected spawn + tick200/end telemetry, stderr tail:\n{proc.stderr[-1500:]}")
            final = rows[-1]
            self.assertEqual(final.get("target"), 4, final)
            self.assertGreaterEqual(final.get("active", 0), 3, f"expected active allied Battle Engines: {final}")
            self.assertEqual(final.get("spawned"), 3, f"expected 3 auxiliary BEs spawned: {final}")
            self.assertGreaterEqual(final.get("ticks", 0), 200, f"expected >=200 sim ticks: {final}")
            self.assertEqual(final.get("nan"), 0, f"NaN position detected: {final}")
            self.assertGreater(final.get("aux_shots", 0), 0, f"expected auxiliary BE weapon fire: {final}")
            self.assertGreater(final.get("hud_focus", 0), 0, f"expected hud_focus hook invocations: {final}")
            self.assertGreaterEqual(final.get("moved", 0), 3, f"expected auxiliary BEs to move cleanly: {final}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_three_battle_engines_live_variant(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-3be-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=3\nmulti_be_cam=1\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=110, quit_frames=580, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 2, proc.stderr[-1500:])
            final = rows[-1]
            self.assertEqual(final.get("target"), 3, final)
            self.assertGreaterEqual(final.get("active", 0), 2, final)
            self.assertEqual(final.get("spawned"), 2, final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertGreater(final.get("aux_shots", 0), 0, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
