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

    // 1b. Solo Skirmish config parsing, activation rules, MP start status formatting, and Walker pitch aim
    Check(CeMultiBeParseSoloSkirmish(nullptr) == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo null -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("") == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo empty -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("0") == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo 0 -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("off") == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo off -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("splitscreen") == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo splitscreen -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("bogus") == CE_SOLO_SKIRMISH_SPLITSCREEN, "solo bogus -> splitscreen");
    Check(CeMultiBeParseSoloSkirmish("1") == CE_SOLO_SKIRMISH_SOLO_AI, "solo 1 -> solo ai");
    Check(CeMultiBeParseSoloSkirmish("ON") == CE_SOLO_SKIRMISH_SOLO_AI, "solo ON -> solo ai");
    Check(CeMultiBeParseSoloSkirmish("true") == CE_SOLO_SKIRMISH_SOLO_AI, "solo true -> solo ai");
    Check(CeMultiBeParseSoloSkirmish("solo") == CE_SOLO_SKIRMISH_SOLO_AI, "solo keyword -> solo ai");
    Check(std::strcmp(CeMultiBeSoloSkirmishIniValue(CE_SOLO_SKIRMISH_SPLITSCREEN), "0") == 0, "ini 0");
    Check(std::strcmp(CeMultiBeSoloSkirmishIniValue(CE_SOLO_SKIRMISH_SOLO_AI), "1") == 0, "ini 1");

    Check(CeMultiBeEffectiveTargetCount(0, false, 851) == 0, "default MP stays inactive");
    Check(CeMultiBeEffectiveTargetCount(0, true, 110) == 0, "solo skirmish never activates on campaign level");
    Check(CeMultiBeEffectiveTargetCount(0, true, 849) == 0, "solo skirmish lower bound 850");
    Check(CeMultiBeEffectiveTargetCount(0, true, 900) == 0, "solo skirmish upper bound 899");
    Check(CeMultiBeEffectiveTargetCount(0, true, 850) == 2, "solo skirmish 1v1 on 850");
    Check(CeMultiBeEffectiveTargetCount(0, true, 851) == 2, "solo skirmish 1v1 on 851");
    Check(CeMultiBeEffectiveTargetCount(0, true, 899) == 2, "solo skirmish 1v1 on 899");
    Check(CeMultiBeEffectiveTargetCount(3, true, 851) == 3, "solo skirmish + 3 BEs");
    Check(CeMultiBeEffectiveTargetCount(4, true, 850) == 4, "solo skirmish + 4 BEs (2v2)");
    Check(CeMultiBeEffectiveTargetCount(4, false, 850) == 4, "multi_be=4 works on skirmish 850");
    Check(CeMultiBeEffectiveTargetCount(4, false, 110) == 0, "multi_be=4 does NOT bleed into campaign without explicit opt-in");
    Check(CeMultiBeEffectiveTargetCount(3, false, 110) == 0, "multi_be=3 does NOT bleed into campaign without explicit opt-in");
    Check(CeMultiBeEffectiveTargetCount(4, false, 110, true) == 4, "multi_be=4 works on campaign when explicitly opted in");

    char mpStatus[128] = {};
    Check(!CeMultiBeFormatMpStartStatus(false, CE_MULTIBE_SIDE_FORSETI, CE_MULTIBE_CHOICE_STANDARD, mpStatus, sizeof(mpStatus)),
          "retail default prints no status line");
    Check(CeMultiBeFormatMpStartStatus(true, CE_MULTIBE_SIDE_FORSETI, CE_MULTIBE_CHOICE_STANDARD, mpStatus, sizeof(mpStatus)) &&
          std::strstr(mpStatus, "Solo vs AI") != nullptr && std::strstr(mpStatus, "Forseti") != nullptr &&
          std::strstr(mpStatus, "Standard (2)") != nullptr && std::strchr(mpStatus, '|') == nullptr,
          "solo default status line");
    Check(CeMultiBeFormatMpStartStatus(true, CE_MULTIBE_SIDE_MUSPELL, CE_MULTIBE_CHOICE_FOUR, mpStatus, sizeof(mpStatus)) &&
          std::strstr(mpStatus, "Solo vs AI") != nullptr && std::strstr(mpStatus, "Muspell") != nullptr &&
          std::strstr(mpStatus, "4 BEs (2v2)") != nullptr,
          "solo muspell 2v2 status line");
    Check(CeMultiBeFormatMpStartStatus(false, CE_MULTIBE_SIDE_MUSPELL, CE_MULTIBE_CHOICE_THREE, mpStatus, sizeof(mpStatus)) &&
          std::strstr(mpStatus, "Solo vs AI") == nullptr && std::strstr(mpStatus, "Muspell") != nullptr &&
          std::strstr(mpStatus, "3 BEs") != nullptr,
          "splitscreen muspell 3 BEs status line");

    Check(CeMultiBeComputeWalkerPitchInput(0.0f, 0.02f) == 0.0f, "walker pitch deadband");
    Check(CeMultiBeComputeWalkerPitchInput(0.0f, 0.25f) < -0.1f, "positive pitch error gives negative vy to pitch +");
    Check(CeMultiBeComputeWalkerPitchInput(0.0f, -0.25f) > 0.1f, "negative pitch error gives positive vy to pitch -");
    Check(CeMultiBeComputeWalkerPitchInput(0.0f, 1.5f) >= -0.75f && CeMultiBeComputeWalkerPitchInput(0.0f, -1.5f) <= 0.75f,
          "walker pitch clamped to [-0.75, 0.75]");

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
    Check(std::strstr(banner, "2v2  FORSETI 2/2 (K:1)  vs  MUSPELL 1/2 (K:0)  (YOU: MUSPELL)") != nullptr &&
          std::strstr(banner, "VERSUS") == nullptr && std::strchr(banner, '[') == nullptr, "2v2 banner text");
    ms.forsetiTotal = 2; ms.muspellTotal = 1;
    ms.FormatHudBanner(true, CE_MULTIBE_SIDE_FORSETI, banner, sizeof(banner));
    Check(std::strstr(banner, "3-BE  FORSETI") != nullptr && std::strstr(banner, "(YOU: FORSETI)") != nullptr &&
          std::strstr(banner, "2v2") == nullptr, "3-BE versus banner text");
    ms.forsetiTotal = 1; ms.muspellTotal = 1;
    ms.FormatHudBanner(true, CE_MULTIBE_SIDE_FORSETI, banner, sizeof(banner));
    Check(std::strstr(banner, "1v1  FORSETI") != nullptr && std::strstr(banner, "(YOU: FORSETI)") != nullptr, "1v1 versus banner text");
    ms.forsetiTotal = 2; ms.muspellTotal = 2;
    ms.walkerCount = 2; ms.jetCount = 2;
    ms.FormatHudBanner(false, CE_MULTIBE_SIDE_FORSETI, banner, sizeof(banner));
    Check(std::strstr(banner, "4-BE SQUAD") != nullptr && std::strstr(banner, "JET 2") != nullptr, "4-BE squad banner text");

    // 6b. Dry-ground spiral search produces 32 distinct non-zero candidates even when base offset is (0, 0)
    {
        float pts[32][2];
        int k = 0;
        for (int si = 0; si < 4; ++si)
            for (int ai = 0; ai < 8; ++ai) {
                CeMultiBeDrySearchOffset(si, ai, 0.0f, 0.0f, &pts[k][0], &pts[k][1]);
                Check(pts[k][0] * pts[k][0] + pts[k][1] * pts[k][1] >= 25.0f, "zero-base spiral offset is non-zero");
                ++k;
            }
        int distinct = 0;
        for (int i = 0; i < 32; ++i) {
            bool dup = false;
            for (int j = 0; j < i; ++j) {
                float dx = pts[i][0] - pts[j][0], dy = pts[i][1] - pts[j][1];
                if (dx * dx + dy * dy < 1.0f) dup = true;
            }
            if (!dup) ++distinct;
        }
        Check(distinct == 32, "zero-base spiral search explores 32 distinct dry candidates");
    }

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

    // 7b. O(1) angle wrapping on extreme finite & non-finite floats (never hangs)
    const float wHugePos = CeMultiBeWrapAngle(1e10f);
    const float wHugeNeg = CeMultiBeWrapAngle(-1e30f);
    Check(std::isfinite(wHugePos) && std::fabs(wHugePos) <= 3.1416f, "CeMultiBeWrapAngle O(1) on 1e10f");
    Check(std::isfinite(wHugeNeg) && std::fabs(wHugeNeg) <= 3.1416f, "CeMultiBeWrapAngle O(1) on -1e30f");
    Check(CeMultiBeWrapAngle(nanv) == 0.0f && CeMultiBeWrapAngle(infv) == 0.0f, "CeMultiBeWrapAngle non-finite -> 0");

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

    // 9. Solo Skirmish dual-slot controls & non-local/AI Battle Engine audio isolation
    Check(CeMultiBeShouldUseSoloControls(false, true, false), "Solo Skirmish enables dual-slot P1 controls");
    Check(CeMultiBeShouldUseSoloControls(false, false, true), "one-screen Commander enables dual-slot P1 controls");
    Check(!CeMultiBeShouldUseSoloControls(false, false, false), "2P splitscreen keeps single-slot per-player controls");
    Check(!CeMultiBeShouldUseSoloControls(true, true, true), "fenced/replay mode disables solo controls override");

    // Faithful/replay fence always preserves retail audio path
    Check(CeMultiBeShouldUseLocalAudio(true, true, 1, true, false, 0, true, false, false), "faithful fence preserves retail audio");
    // Solo Skirmish: slot 0 human keeps local HUD/cockpit audio; AI P2 & aux bots use 3D positional audio and no HUD voices
    Check(CeMultiBeShouldUseLocalAudio(false, true, 0, false, false, 0, true, true, true), "Solo Skirmish P1 uses local audio");
    Check(!CeMultiBeShouldUseLocalAudio(false, true, 1, true, false, 0, true, false, true), "Solo Skirmish AI P2 silenced from 2D audio");
    Check(!CeMultiBeShouldUseLocalAudio(false, true, 2, true, false, 0, false, false, true), "2v2 AI aux slot 2 silenced from 2D audio");
    // Frame-0 fallback before gEngines populates
    Check(!CeMultiBeShouldUseLocalAudio(false, false, -1, false, false, 0, true, false, true), "Solo frame-0 non-P0 BE silenced from 2D audio");
    Check(CeMultiBeShouldUseLocalAudio(false, false, -1, false, false, 0, true, true, true), "Solo frame-0 P0 BE uses local audio");
    // Local 2P splitscreen: both human P1 and P2 keep local audio
    Check(CeMultiBeShouldUseLocalAudio(false, false, -1, false, false, 0, false, false, true), "2P splitscreen both humans keep local audio");
    Check(CeMultiBeShouldUseLocalAudio(false, true, 1, false, false, 0, false, false, true), "2v2 splitscreen human P2 keeps local audio");
    // Online match: only local player slot gets 2D HUD/cockpit audio, remote peer gets 3D audio
    Check(CeMultiBeShouldUseLocalAudio(false, true, 1, false, true, 1, false, false, true), "Online local slot 1 uses local audio");
    Check(!CeMultiBeShouldUseLocalAudio(false, true, 0, false, true, 1, false, true, false), "Online remote slot 0 silenced from 2D audio");
    Check(!CeMultiBeShouldUseLocalAudio(false, false, -1, false, true, 1, false, true, false), "Online 2P remote peer silenced from 2D audio");

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


def run_bea_offscreen(exe, game_dir, run_dir, level, quit_frames, ce_ini_text=None, ce_layer=True, shot_at=None, fake_keys=None, extra_env=None, real_clock=False, res=(1280, 720)):
    """Runs `bea` 100% offscreen with isolated user dir, returning CompletedProcess.
    When `level` is None or `real_clock` is True, runs on the real clock paced at 30 Hz
    so widescreen, HUD anchoring, and Front-End transitions run identically to live play."""
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
        "DXVK_LOG_LEVEL": "none",
        "XDG_CACHE_HOME": str(run_dir / "cache"),
        "XDG_RUNTIME_DIR": str(run_dir / "runtime"),
        "BEA_USER_DIR": str(user_dir),
        "BEA_GAME_DIR": str(game_dir),
        "BEA_NO_UPDATE_CHECK": "1",
        "BEA_PORT_QUIT": str(quit_frames),
    }
    if level is None or real_clock:
        env["BEA_PORT_FEEL_LOG"] = str(run_dir / "feel.log")
        env["BEA_PORT_FEEL_PACE_HZ"] = "30"
    else:
        env["BEA_PORT_CLOCK"] = "fixed"
        env["BEA_PORT_CLOCK_DT"] = "0.05"
        if ce_layer:
            env["BEA_PORT_CE_LAYER"] = "1"
    if shot_at is not None:
        env["BEA_PORT_SHOT"] = f"{shot_at[0]}:{shot_at[1]}"
    if fake_keys is not None:
        env["BEA_PORT_FAKE_KEYS"] = fake_keys
    if extra_env:
        env.update(extra_env)
    libs = resolve_runtime_libs()
    exe_lib = Path(exe).resolve().parent / "lib"
    if exe_lib.is_dir():
        libs = [str(exe_lib)] + libs
    if libs:
        env["LD_LIBRARY_PATH"] = ":".join(libs)
    cmd = [str(exe)]
    if level is not None:
        cmd += ["-level", str(level)]
    cmd += ["-skipfmv", "-forcewindowed", "-res", str(res[0]), str(res[1])]
    return subprocess.run(cmd, cwd=str(game_dir), env=env, capture_output=True, text=True, timeout=180)


def build_fake_keys(taps):
    """Builds a BEA_PORT_FAKE_KEYS string from [(present, key_name), ...] taps."""
    key_map = {
        "RETURN": (40, 0x1C),
        "ESC": (41, 0x01),
        "SPACE": (44, 0x39),
        "RIGHT": (79, 0x14D),
        "LEFT": (80, 0x14B),
        "DOWN": (81, 0x150),
        "UP": (82, 0x148),
        "M": (16, 0x32),
        "E": (8, 0x12),
    }
    tokens = []
    for present, name in taps:
        sdl, pc = key_map[name]
        tokens.append((present, len(tokens), f"{present}:{sdl}:{pc:x}:d"))
        tokens.append((present + 2, len(tokens), f"{present + 2}:{sdl}:{pc:x}:u"))
    tokens.sort(key=lambda item: (item[0], item[1]))
    return ",".join(tok for _, _, tok in tokens)


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
            "solo skirmish defaults on": (
                "\treturn CE_SOLO_SKIRMISH_SPLITSCREEN;\n}\n\ninline const char *CeMultiBeSoloSkirmishIniValue",
                "\treturn CE_SOLO_SKIRMISH_SOLO_AI;\n}\n\ninline const char *CeMultiBeSoloSkirmishIniValue",
            ),
            "solo skirmish activates on campaign": (
                "\tif (soloSkirmish && isSkirmish)\n\t\treturn 2;",
                "\tif (soloSkirmish)\n\t\treturn 2;",
            ),
            "solo skirmish fails to activate 1v1": (
                "\tif (soloSkirmish && isSkirmish)\n\t\treturn 2;",
                "\tif (soloSkirmish && isSkirmish)\n\t\treturn 0;",
            ),
            "multi_be bleeds into campaign without opt-in": (
                "\tif (multiBeCount >= 3 && (isSkirmish || allowCampaign))",
                "\tif (multiBeCount >= 3)",
            ),
            "3-be versus banner still prints 2v2": (
                'const char *modeTag = (total == 3) ? "3-BE" : (total == 2 ? "1v1" : "2v2");',
                'const char *modeTag = "2v2";',
            ),
            "mp status draws on retail default": (
                "\tif (!soloSkirmish && playSide == CE_MULTIBE_SIDE_FORSETI && modeChoice == CE_MULTIBE_CHOICE_STANDARD)",
                "\tif (false)",
            ),
            "walker pitch sign inverted": (
                "\treturn CeMultiBeClamp(-pitchErr * 1.6f, -0.75f, 0.75f);",
                "\treturn CeMultiBeClamp(pitchErr * 1.6f, -0.75f, 0.75f);",
            ),
            "solo controls bypasses fence": (
                "\tif (fenced)\n\t\treturn false;\n\treturn soloSkirmish || oneScreenMatch;",
                "\treturn soloSkirmish || oneScreenMatch;",
            ),
            "solo controls ignores soloSkirmish": (
                "\treturn soloSkirmish || oneScreenMatch;",
                "\treturn oneScreenMatch;",
            ),
            "local audio leaks AI cockpit sounds": (
                "\t\tif (slotIsAi)\n\t\t\treturn false;",
                "\t\tif (slotIsAi)\n\t\t\treturn true;",
            ),
            "local audio leaks remote online HUD": (
                "\t\tif (onlineActive)\n\t\t\treturn slotIndex == localOnlineSlot;",
                "\t\tif (onlineActive)\n\t\t\treturn true;",
            ),
            "local audio ignores faithful fence": (
                "\tif (faithfulFence)\n\t\treturn true;",
                "\tif (faithfulFence)\n\t\treturn false;",
            ),
        }
        for name, (old, new) in s_mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, s_src, name)
                rc, _ = self.compile_and_run(h_src, s_src.replace(old, new, 1), name.replace(" ", "-"), sanitize=False)
                self.assertNotEqual(rc, 0, f"mutant {name} was not caught")


class GateAuditTests(unittest.TestCase):
    def test_no_new_bea_env_vars_and_spdx_present(self):
        allowed_fence_envs = {"BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"}
        for rel in ("game/ce_multibe.h", "game/ce_multibe_state.h", "game/ce_multibe_ai.h", "game/ce_multibe.cpp",
                    "game/ai/ce_ai_pilot_l1.h", "game/ai/ce_ai_pilot_l1.cpp"):
            text = (PORT / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200], rel)
            found = set(re.findall(r"BEA_[A-Z0-9_]+", text))
            if rel == "game/ce_multibe.cpp":
                self.assertEqual(found, allowed_fence_envs, f"{rel} may only reference pilot fence env vars, got {found}")
            else:
                self.assertEqual(found, set(), f"{rel} must not reference BEA_* env vars, got {found}")

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
        self.assertIn("if (MultiBeFenced())", src, "PortCeMultiBeLevelBegin and PortCeSoloSkirmishActive must share MultiBeFenced()")
        for fence_term in ("PLAYABLE_DEMO", "CLIPARAMS.mAutoConfigTest", "CLIPARAMS.mRecordDemo",
                           "CLIPARAMS.mPlaybackDemo", "CLIPARAMS.mStressTest", "CLIPARAMS.mAttractMode",
                           "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT"):
            self.assertIn(fence_term, src, f"MultiBeFenced() must guard against {fence_term}")
        # Verify PortCeSoloSkirmishActive uses cached/latched state (zero per-frame ce.ini disk I/O)
        solo_fn = src[src.index('extern "C" int PortCeSoloSkirmishActive('):src.index('extern "C" void PortCeMultiBeDrawMpStartStatus(')]
        self.assertIn("MultiBeFenced()", solo_fn, "PortCeSoloSkirmishActive must check MultiBeFenced()")
        self.assertIn("gLatchedSoloSkirmish", solo_fn, "PortCeSoloSkirmishActive must return latched in-level state")
        self.assertNotIn("CE_SettingGetText", solo_fn, "PortCeSoloSkirmishActive must not read ce.ini per frame")
        self.assertNotIn("CE_SettingIs", solo_fn, "PortCeSoloSkirmishActive must not read ce.ini per frame")
        read_solo_fn = src[src.index("int ReadSoloSkirmishSetting()"):src.index("// Native clickable")]
        self.assertIn("gCachedSoloSkirmish", read_solo_fn, "ReadSoloSkirmishSetting must cache ce.ini value")
        self.assertNotIn("CE_SettingGetText", read_solo_fn, "ReadSoloSkirmishSetting must lazy-load via cache, not read disk unconditionally")
        self.assertIn("gCachedSoloSkirmish = choice;", src, "CCeSoloSkirmishItem::SetValue must update cache synchronously")
        self.assertIn("gCachedPlaySide = side;", src, "CCePlaySideItem::SetValue must update cache synchronously")
        self.assertIn("gCachedMultiBeChoice = choice;", src, "CCeBattleEnginesItem::SetValue must update cache synchronously")
        l1_src = (PORT / "game/ai/ce_ai_pilot_l1.cpp").read_text(encoding="utf-8")
        combined_src = src + "\n" + l1_src
        self.assertNotIn("GetUnitNB().First()", combined_src, "must not call WORLD.GetUnitNB().First(); use ListIterator<CUnit>")
        self.assertNotIn("GetUnitNB().Next()", combined_src, "must not call WORLD.GetUnitNB().Next(); use ListIterator<CUnit>")
        self.assertNotIn("mNodes.First()", combined_src, "must not call INFLUENCEMAP->mNodes.First(); use ListIterator<CInfluenceNode>")
        self.assertGreaterEqual(combined_src.count("ListIterator<CUnit>"), 4, "all unit scans must use stack-local ListIterator<CUnit>")
        self.assertIn("ListIterator<CInfluenceNode>", combined_src, "battle-line scan must use stack-local ListIterator<CInfluenceNode>")
        for ai_fn in ("CeMultiBeComputeLeadPoint", "CeMultiBeEvaluateFireGate", "CeMultiBePickWeapon",
                      "CeMultiBeComputeWalkerMove", "CeMultiBeStepWalkerDash", "CeMultiBeComputeJetFlight",
                      "CeMultiBeScoreTarget", "CeMultiBeEvaluateSurvival", "CeMultiBeShouldMorphSmart"):
            self.assertIn(ai_fn, l1_src, f"ce_ai_pilot_l1.cpp must wire fair-rules AI helper {ai_fn}")
        self.assertIn("MAP.GetWaterLevel()", src, "must check MAP.GetWaterLevel() when spawning/patrolling aux BEs")
        self.assertIn("PortCeMultiBeHandleDeath", src, "must intercept aux BE StartDieProcess cleanly")
        self.assertIn("focused->GetPlayer()->GetNumber() == player->GetNumber()", src, "2P split-screen hud_focus must match player numbers")
        self.assertIn("PortRecording()", src, "must guard start slot and side inversion against PortRecording()")
        self.assertIn("PortCeMultiBeAllowTeamKills", src, "must provide CE-layer gate for Muspell ConfirmedKill")
        self.assertIn('L"Play side: "', src, "clickable Play side menu row missing")
        self.assertIn('L"Skirmish mode: "', src, "clickable Skirmish mode menu row missing")
        self.assertIn('L"Battle Engines: "', src, "clickable Battle Engines menu row missing")
        self.assertIn("PortCeSoloSkirmishActive", src, "PortCeSoloSkirmishActive definition missing")
        self.assertIn("SyncHumanPlayerEngines", src, "human player respawn synchronization missing")
        self.assertIn("SyncSoloP2Engine", src, "Solo Skirmish P2 respawn synchronization missing")
        self.assertIn("tgtSurfaceZ = (tgtGroundZ < waterZ) ? tgtGroundZ : waterZ;", l1_src, "target altitude must measure above min(ground, water)")
        self.assertIn("CalcUnitOverCrossHair(NULL, TRUE, TRUE)", l1_src, "aux BE weapon/lock calls must refresh CalcUnitOverCrossHair under bound camera")
        self.assertIn("curMode != NULL", l1_src, "CWeapon::CanLock must guard against null GetCurrentMode()")
        self.assertIn("CeCmdMatchUpdate()", src, "Solo Skirmish must invoke CeCmdMatchUpdate() even when commander=0")
        self.assertIn("CeCmdMatchLevelEnd()", src, "Solo Skirmish must restore mFullscreenMultiplayer on level_end")
        patch_0375 = (PORT / "patches/0375-ce-solo-skirmish.patch").read_text(encoding="latin-1")
        self.assertIn("PortCeSoloSkirmishActive", patch_0375, "patch 0375 must wire PortCeSoloSkirmishActive")
        self.assertIn(
            "if (GAME.IsMultiplayer() && (this == GAME.GetController(1)) && PortCeSoloSkirmishActive && PortCeSoloSkirmishActive())",
            patch_0375,
            "patch 0375 must short-circuit Controller.cpp on GAME.IsMultiplayer() && controller 1 first",
        )
        self.assertIn(
            "if (!GAME.IsMultiplayer() || !(PortCeSoloSkirmishActive && PortCeSoloSkirmishActive()))",
            patch_0375,
            "patch 0375 must short-circuit game.cpp Render on !GAME.IsMultiplayer() first",
        )
        patch_0373 = (PORT / "patches/0373-ce-multibe-unit-sound-particles.patch").read_text(encoding="latin-1")
        self.assertIn("PortCeOneScreenMatch()", patch_0373, "patch 0373 must pin 3D audio listener to camera 0 in one-screen matches")
        self.assertIn("PortCeBattleEngineIsLocalHuman", patch_0373, "patch 0373 must isolate non-local/AI Battle Engine 2D gunfire and HUD audio")
        self.assertIn("PortCeSoloControlsActive", patch_0375, "patch 0375 must allow dual-slot 1P controls in Solo Skirmish")
        patch_0369 = (PORT / "patches/0369-ce-multibe-controller-slots.patch").read_text(encoding="latin-1")
        self.assertIn("PortCeSoloSkirmishActive", patch_0369, "patch 0369 must exempt slot 1 from disconnect gate in Solo Skirmish")
        self.assertIn("PlayerInput(int player) const", patch_0369, "patch 0369 must keep SControlBinding at retail 32 bytes via PlayerInput()")
        patch_0370 = (PORT / "patches/0370-ce-multibe-game-state.patch").read_text(encoding="latin-1")
        self.assertIn("static SINT\t\tmPlayerLives[MAX_PLAYERS];", patch_0370, "patch 0370 must keep CGame::mPlayerLives static for retail CGame layout")
        self.assertIn("MEMTYPE_UNKNOWN", src, "ce_multibe.cpp must use MEMTYPE_UNKNOWN instead of raw 0x80")
        self.assertNotIn("(EMemoryType)0x80", src, "ce_multibe.cpp must not hardcode (EMemoryType)0x80")
        lvl_begin = src[src.index('extern "C" void PortCeMultiBeLevelBegin('):src.index('extern "C" void PortCeMultiBeLevelEnd(')]
        self.assertIn("CeCmdMatchLevelEnd();", lvl_begin, "PortCeMultiBeLevelBegin must defensively call CeCmdMatchLevelEnd()")
        render_cam = src[src.index('extern "C" CCamera *PortCeMultiBeRenderCamera('):src.index('extern "C" CBattleEngine *PortCeMultiBeHudFocus(')]
        for guard in ("PortCeCmdMapOpen()", "PortCePlayUnitOwnsPilot(0)", "GAME.IsFreeCameraOn(0)", "camera != GAME.GetCamera(0)"):
            self.assertIn(guard, render_cam, f"PortCeMultiBeRenderCamera must bypass override on {guard}")


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
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=850, quit_frames=700, ce_ini_text=ini)
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
            self.assertLessEqual(float(final.get("max_ai_land_alt", 0.0)), 14.5, f"AI Jet->Walker landing exceeded safe altitude cap: {final}")
            self.assertGreater(final.get("evades", 0), 0, f"expected reactive AI evasion maneuvers: {final}")
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
            self.assertGreater(final.get("weapon_picks", 0), 0, final)
            self.assertGreater(final.get("evades", 0), 0, final)
            self.assertLessEqual(float(final.get("max_ai_land_alt", 0.0)), 14.5, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_multi_be_does_not_bleed_into_campaign_by_default(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-110-nobleed-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=110, quit_frames=520, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            rows = parse_telemetry(proc.stderr)
            self.assertEqual(rows, [], f"multi_be=4 must not bleed into Campaign without multi_be_campaign=1, got {rows}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_four_battle_engines_campaign_live_for_200_plus_ticks(self):
        tmp = Path(tempfile.mkdtemp(prefix="multibe-110-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nmulti_be=4\nmulti_be_campaign=1\nmulti_be_cam=1\nmulti_be_hud_slot=2\n"
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
            ini = "[Experimental]\nmulti_be=3\nmulti_be_campaign=1\nmulti_be_cam=1\n"
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
            self.assertGreater(final.get("weapon_picks", 0), 0, final)
            self.assertGreater(final.get("evades", 0), 0, final)
            self.assertLessEqual(float(final.get("max_ai_land_alt", 0.0)), 14.5, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_solo_skirmish_1v1_forseti_fullscreen_ai_live(self):
        """1v1 Solo Skirmish (commander=0, multi_be=2, solo_skirmish=1): 1 fullscreen viewport + active AI P2."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-solo-1v1-f-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=forseti\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=851, quit_frames=650, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 3, proc.stderr[-1500:])
            mid = next((r for r in rows if r.get("phase") == "tick200"), None)
            self.assertIsNotNone(mid, rows)
            self.assertEqual(mid.get("onescreen"), 1, f"Solo Skirmish must enable fullscreen even with commander=0: {mid}")
            self.assertEqual(mid.get("viewpoints"), 1, f"expected 1 fullscreen viewpoint (zero splitscreen): {mid}")
            final = rows[-1]
            self.assertEqual(final.get("phase"), "end", final)
            self.assertEqual(final.get("onescreen"), 0, f"mFullscreenMultiplayer must restore cleanly on level_end: {final}")
            self.assertEqual(final.get("solo"), 1, final)
            self.assertEqual(final.get("viewpoints"), 1, final)
            self.assertEqual(final.get("target"), 2, final)
            self.assertEqual(final.get("spawned"), 0, f"1v1 Solo Skirmish should spawn 0 auxiliary BEs: {final}")
            self.assertEqual(final.get("side"), "forseti", final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertEqual(final.get("p2_moved"), 1, f"AI-piloted P2 must actively move: {final}")
            self.assertGreater(final.get("p2_shots", 0), 0, f"AI-piloted P2 must actively fire: {final}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_solo_skirmish_1v1_muspell_fullscreen_ai_live(self):
        """1v1 Solo Skirmish as Muspell (solo_skirmish=1, multi_be=2, play_side=muspell)."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-solo-1v1-m-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=muspell\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=851, quit_frames=650, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 3, proc.stderr[-1500:])
            mid = next((r for r in rows if r.get("phase") == "tick200"), None)
            self.assertIsNotNone(mid, rows)
            self.assertEqual(mid.get("onescreen"), 1, mid)
            self.assertEqual(mid.get("viewpoints"), 1, mid)
            final = rows[-1]
            self.assertEqual(final.get("onescreen"), 0, f"mFullscreenMultiplayer must restore cleanly on level_end: {final}")
            self.assertEqual(final.get("solo"), 1, final)
            self.assertEqual(final.get("viewpoints"), 1, final)
            self.assertEqual(final.get("target"), 2, final)
            self.assertEqual(final.get("spawned"), 0, final)
            self.assertEqual(final.get("side"), "muspell", final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertEqual(final.get("p2_moved"), 1, final)
            self.assertGreater(final.get("p2_shots", 0), 0, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_solo_skirmish_2v2_muspell_frontend_launch_with_commander_and_playunit(self):
        """Front-End launch WITHOUT -level: Title -> Main -> Multiplayer -> Level 851 (Versus) -> Select Config -> Play 2v2
        Solo vs AI as Muspell with Commander (M) and Play Any Unit (E) enabled, proving zero P2 binding block & 1 fullscreen view."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-solo-fe-2v2-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nsolo_skirmish=1\nmulti_be=4\nplay_side=muspell\ncommander=1\nplay_unit=1\n"
            # Title (FEP_INTRO): RETURN -> Main Menu (FEP_MAIN): clamp to row 7 (Quit) via 8x DOWN,
            # then 4x UP to row 3 (Multiplayer) -> RETURN -> FEPMultiplayer: DOWN to (0,1)=851 (Versus) -> RETURN
            # -> FEPMultiplayerStart -> RETURN (starts immediately with zero P2 input or binding prompt!)
            # In-game: tap M to open Commander Mode, tap M again to close, tap E for Play Any Unit.
            keys = build_fake_keys([
                (105, "RETURN"),
                (195, "DOWN"), (205, "DOWN"), (215, "DOWN"), (225, "DOWN"),
                (235, "DOWN"), (245, "DOWN"), (255, "DOWN"), (265, "DOWN"),
                (278, "UP"), (288, "UP"), (298, "UP"), (308, "UP"),
                (322, "RETURN"),
                (430, "DOWN"),
                (445, "RETURN"),
                (520, "RETURN"),
                (760, "M"),
                (820, "M"),
                (860, "E"),
            ])
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=None, quit_frames=960, ce_ini_text=ini, fake_keys=keys)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 3, f"Front-End launch did not enter Solo Skirmish level 851:\n{proc.stderr[-1500:]}")
            mid = next((r for r in rows if r.get("phase") == "tick200"), None)
            self.assertIsNotNone(mid, rows)
            self.assertEqual(mid.get("onescreen"), 1, mid)
            self.assertEqual(mid.get("viewpoints"), 1, mid)
            final = rows[-1]
            self.assertEqual(final.get("level"), 851, final)
            self.assertEqual(final.get("solo"), 1, final)
            self.assertEqual(final.get("onescreen"), 0, final)
            self.assertEqual(final.get("viewpoints"), 1, final)
            self.assertEqual(final.get("target"), 4, final)
            self.assertEqual(final.get("spawned"), 2, final)
            self.assertEqual(final.get("side"), "muspell", final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertEqual(final.get("p2_moved"), 1, final)
            self.assertGreater(final.get("p2_shots", 0), 0, final)
            self.assertGreater(final.get("aux_shots", 0), 0, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_pilot_env_fence_disables_solo_skirmish_and_multibe(self):
        """BEA_PORT_PILOT=1 must fence Solo Skirmish and Multi-BE completely off even with BEA_PORT_CE_LAYER=1."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-pilot-fence-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nsolo_skirmish=1\nmulti_be=4\nplay_side=muspell\n"
            proc = run_bea_offscreen(
                self.exe, self.game_dir, tmp, level=851, quit_frames=520,
                ce_ini_text=ini, ce_layer=True, extra_env={"BEA_PORT_PILOT": "1"},
            )
            self.assertEqual(proc.returncode, 0, proc.stderr[-1500:])
            rows = parse_telemetry(proc.stderr)
            self.assertEqual(rows, [], f"MultiBeFenced() must disable Solo Skirmish/Multi-BE under BEA_PORT_PILOT: {rows}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_solo_skirmish_coop_level_850_ai_wingman_live(self):
        """Solo Skirmish on Co-op map Level 850 (solo_skirmish=1, multi_be=2): P1 fullscreen + Forseti AI wingman P2."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-solo-850-", dir=SCRATCH))
        try:
            ini = "[Experimental]\nsolo_skirmish=1\nmulti_be=2\nplay_side=forseti\n"
            proc = run_bea_offscreen(self.exe, self.game_dir, tmp, level=850, quit_frames=650, ce_ini_text=ini)
            self.assertEqual(proc.returncode, 0, proc.stderr[-2000:])
            rows = parse_telemetry(proc.stderr)
            self.assertGreaterEqual(len(rows), 3, proc.stderr[-1500:])
            mid = next((r for r in rows if r.get("phase") == "tick200"), None)
            self.assertIsNotNone(mid, rows)
            self.assertEqual(mid.get("onescreen"), 1, mid)
            self.assertEqual(mid.get("viewpoints"), 1, mid)
            final = rows[-1]
            self.assertEqual(final.get("level"), 850, final)
            self.assertEqual(final.get("solo"), 1, final)
            self.assertEqual(final.get("onescreen"), 0, final)
            self.assertEqual(final.get("viewpoints"), 1, final)
            self.assertEqual(final.get("target"), 2, final)
            self.assertEqual(final.get("spawned"), 0, final)
            self.assertGreaterEqual(final.get("ticks", 0), 200, final)
            self.assertEqual(final.get("nan"), 0, final)
            self.assertEqual(final.get("p2_moved"), 1, final)
            self.assertGreater(final.get("p2_shots", 0), 0, final)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_solo_skirmish_widescreen_1080p_aspect_hud_and_ai_landing_live(self):
        """Real-clock 1920x1080 widescreen Solo vs AI Skirmish vs 2P Splitscreen control:
        proves camera aspect (0.75 vs 0.50), HUD corner anchoring (inset 0 vs 240), and low-altitude AI landing."""
        tmp = Path(tempfile.mkdtemp(prefix="multibe-solo-1080p-", dir=SCRATCH))
        try:
            ini_solo = "[Experimental]\nsolo_skirmish=1\nmulti_be=4\nplay_side=forseti\n"
            proc_solo = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "solo", level=851, quit_frames=520,
                ce_ini_text=ini_solo, real_clock=True, res=(1920, 1080),
                extra_env={"BEA_PORT_WIDE_TEST": "1920x1080"},
            )
            self.assertEqual(proc_solo.returncode, 0, proc_solo.stderr[-2000:])
            rows_solo = parse_telemetry(proc_solo.stderr)
            self.assertGreaterEqual(len(rows_solo), 2, proc_solo.stderr[-1500:])
            mid = next((r for r in rows_solo if r.get("phase") == "tick200"), rows_solo[-1])
            self.assertEqual(mid.get("viewpoints"), 1, mid)
            self.assertAlmostEqual(float(mid.get("cam_aspect", 0.0)), 0.75, places=2, msg=f"Solo Skirmish must use 0.75 camera aspect, not 0.50: {mid}")
            self.assertEqual(mid.get("hud_anchored"), 1, f"Solo Skirmish 1080p must anchor HUD corners: {mid}")
            self.assertAlmostEqual(float(mid.get("hud_inset", 999.0)), 0.0, places=1, msg=f"Solo Skirmish 1080p must have zero 4:3 inset: {mid}")
            final_solo = rows_solo[-1]
            self.assertLessEqual(float(final_solo.get("max_ai_land_alt", 0.0)), 14.5, f"AI Jet->Walker landing exceeded safe altitude cap: {final_solo}")

            ini_split = "[Experimental]\nsolo_skirmish=0\ncommander=0\nmulti_be=4\nplay_side=forseti\n"
            proc_split = run_bea_offscreen(
                self.exe, self.game_dir, tmp / "split", level=851, quit_frames=420,
                ce_ini_text=ini_split, real_clock=True, res=(1920, 1080),
                extra_env={"BEA_PORT_WIDE_TEST": "1920x1080"},
            )
            self.assertEqual(proc_split.returncode, 0, proc_split.stderr[-2000:])
            rows_split = parse_telemetry(proc_split.stderr)
            self.assertGreaterEqual(len(rows_split), 2, proc_split.stderr[-1500:])
            mid_s = next((r for r in rows_split if r.get("phase") == "tick200"), rows_split[-1])
            self.assertEqual(mid_s.get("viewpoints"), 2, mid_s)
            self.assertAlmostEqual(float(mid_s.get("cam_aspect", 0.0)), 0.50, places=2, msg=f"2P Splitscreen must keep 0.50 aspect: {mid_s}")
            self.assertEqual(mid_s.get("hud_anchored"), 0, f"2P Splitscreen must NOT anchor HUD corners: {mid_s}")
            self.assertAlmostEqual(float(mid_s.get("hud_inset", 0.0)), 240.0, places=1, msg=f"2P Splitscreen 1080p must keep 240px 4:3 inset: {mid_s}")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
