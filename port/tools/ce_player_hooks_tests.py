#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wave 2 Phase 3 tests for CePlayerHooks, 0396-ce-hook-player-act, match-local spawnSeq,
team-parameterized ICommanderPort, headless 4-slot IUnitPossessionPort, and Layer-1/2 AI.

    python3 port/tools/ce_player_hooks_tests.py [-v]

Verifies under host ASan/UBSan + causal mutants:
  1. Cross-level spawnSeq sort invariance (differing worldEpoch / slot generation histories produce
     bit-identical entity order; mutant sorting by CeEntityHandle numeric bits is caught).
  2. Fog-of-war parity (off-scanner enemies beyond 90m excluded; mutant dropping filter is caught).
  3. Toggle-off refusals (CE_TOGGLE_COMMANDER, CE_TOGGLE_PLAY_UNIT, CE_TOGGLE_FLIGHT).
  4. Cross-team Commander rejection (CE_WHY_ENEMY) and Layer-2 0-CPU dormancy when toggles are OFF.
  5. Headless 4-slot Play-Any-Unit mutual exclusion, human pilot protection, and clean release.
  6. Standalone CE_NETWORK_OFF header compile & zero GetCurrentWeapon() / CeEntityHandleFor() leaks.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
GAME = PORT / "game"
SCRATCH = ROOT / "local-data/port/ce-player-hooks-tests"


DRIVER_CPP = r'''
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "ce_entity_handles.h"
#include "ce_policy.h"
#include "ce_player_hooks.h"
#include "ce_playunit_slots.h"
#include "ai/ce_ai_pilot_l1.h"
#include "ai/ce_ai_tactical_l2.h"

static uint32_t HashObsEntities(const CePlayerObs &obs) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0u; i < obs.entityCount; ++i) {
        const CePlayerEntityObs &e = obs.entities[i];
        // Note: intentionally hash spawnSeq & distSqQuantized & class/pos, never raw handle bits!
        const uint32_t words[5] = {
            e.spawnSeq, e.distSqQuantized,
            (uint32_t)e.allegiance,
            (uint32_t)(e.targetClass | (e.isFriendly << 8)),
            (uint32_t)(int32_t)(e.pos[0] * 16.0f)
        };
        for (uint32_t w : words) h = (h ^ w) * 16777619u;
    }
    return h;
}

static int TestCrossLevelSpawnSeqSortInvariance() {
    // Client 1 played 4 SP missions before joining (slot generations & worldEpoch advanced asymmetrically).
    CeEntityHandleTable peerA;
    for (int ep = 0; ep < 4; ++ep) {
        peerA.Register(0x10000u + ep * 0x100u, 1u);
        peerA.Register(0x20000u + ep * 0x100u, 2u);
        peerA.ResetWorld();
    }
    // Client 2 booted fresh.
    CeEntityHandleTable peerB;

    // Both peers enter the same match and spawn 4 units in identical SIM order at different heap addresses.
    const uintptr_t addrsA[4] = {0x90010u, 0x40020u, 0x80030u, 0x30040u};
    const uintptr_t addrsB[4] = {0x10010u, 0x70020u, 0x20030u, 0x60040u};
    CeEntityHandle hA[4], hB[4];
    for (int i = 0; i < 4; ++i) {
        hA[i] = peerA.RegisterFresh(addrsA[i], 100u + i);
        hB[i] = peerB.RegisterFresh(addrsB[i], 100u + i);
    }
    // Verify generations actually differ between peers while match-local spawnSeq is identical (1..4)!
    if (peerA.WorldEpoch() == peerB.WorldEpoch()) { std::puts("FAIL epochs must differ"); return 1; }
    if (CeEntityHandleGeneration(hA[0]) == CeEntityHandleGeneration(hB[0])) {
        std::puts("FAIL generations must differ"); return 2;
    }
    for (int i = 0; i < 4; ++i) {
        if (peerA.SpawnSeqOf(addrsA[i]) != (uint32_t)(i + 1) ||
            peerB.SpawnSeqOf(addrsB[i]) != (uint32_t)(i + 1)) {
            std::puts("FAIL match-local spawnSeq mismatch"); return 3;
        }
    }

    // Build two obs with equal distances (ties broken by spawnSeq), inserted in reversed order.
    CePlayerObs obsA = {}, obsB = {};
    obsA.entityCount = 4u;
    obsB.entityCount = 4u;
    // Also inject a third peer where slot 0 has a HIGHER numeric handle than slot 3 on A, reversed on B.
    const int rev[4] = {3, 1, 2, 0};
    for (int k = 0; k < 4; ++k) {
        int i = rev[k];
        obsA.entities[k].handle = hA[i];
        obsA.entities[k].spawnSeq = peerA.SpawnSeqOf(addrsA[i]);
        obsA.entities[k].distSqQuantized = 1600u; // exact tie!
        obsA.entities[k].allegiance = 1;
        obsA.entities[k].pos[0] = 100.0f + float(i);

        obsB.entities[k].handle = hB[i];
        obsB.entities[k].spawnSeq = peerB.SpawnSeqOf(addrsB[i]);
        obsB.entities[k].distSqQuantized = 1600u;
        obsB.entities[k].allegiance = 1;
        obsB.entities[k].pos[0] = 100.0f + float(i);
    }
    // Mutate peerA handle numeric ordering so handle-based sort diverges from spawnSeq sort.
    obsA.entities[0].handle = CeEntityHandlePack(1u, 1u);   // spawnSeq=4, tiny handle
    obsA.entities[3].handle = CeEntityHandlePack(99u, 99u); // spawnSeq=1, huge handle

    CePlayerObsSortEntities(obsA.entities, obsA.entityCount);
    CePlayerObsSortEntities(obsB.entities, obsB.entityCount);
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (obsA.entities[i].spawnSeq != i + 1u || obsB.entities[i].spawnSeq != i + 1u) {
            std::puts("FAIL sort did not order by spawnSeq"); return 4;
        }
    }
    if (HashObsEntities(obsA) != HashObsEntities(obsB)) {
        std::puts("FAIL cross-peer obs hash diverged"); return 5;
    }
    return 0;
}

static int TestFogOfWarParity() {
    if (!CePlayerObsEntityVisible(true, false, 500.0f * 500.0f)) { std::puts("FAIL friendly always visible"); return 10; }
    if (!CePlayerObsEntityVisible(false, true, 300.0f * 300.0f)) { std::puts("FAIL on-scanner enemy visible"); return 11; }
    if (!CePlayerObsEntityVisible(false, false, 89.0f * 89.0f)) { std::puts("FAIL close enemy visible"); return 12; }
    if (CePlayerObsEntityVisible(false, false, 95.0f * 95.0f)) { std::puts("FAIL cloaked distant enemy leaked"); return 13; }
    return 0;
}

static int TestToggleOffRefusalsAndL2Dormancy() {
    CePlayerAction act = {};
    act.pilot.morph = 1u;
    act.cmd.valid = 1u;
    act.cmd.verb = 2;
    act.possess.valid = 1u;
    act.possess.unitHandle = CeEntityHandlePack(5u, 1u);

    // All toggles OFF -> morph, cmd, and possess MUST all be masked out.
    CePlayerSanitizeAction(CE_TOGGLE_NONE, &act);
    if (act.pilot.morph != 0u || act.cmd.valid != 0u || act.possess.valid != 0u) {
        std::puts("FAIL sanitize leaked disabled toggle"); return 20;
    }

    // Layer 2 must remain strictly dormant when CE_TOGGLE_COMMANDER and CE_TOGGLE_PLAY_UNIT are OFF.
    CeAiTacticalL2State st;
    CeAiTacticalL2Reset(&st);
    CePlayerObs obs = {};
    obs.simTick = 30u;
    obs.slot = 0u;
    obs.lifeRatio = 1.0f;
    obs.activeToggles = CE_TOGGLE_AI_PILOTS | CE_TOGGLE_FLIGHT;
    obs.entityCount = 1u;
    obs.entities[0].isFriendly = 0u;
    obs.entities[0].distSqQuantized = 1000u;
    obs.entities[0].pos[0] = 200.0f; obs.entities[0].pos[1] = 200.0f;
    obs.squadCount = 1u;
    obs.squads[0].squadHandle = CeEntityHandlePack(3u, 1u);
    obs.squads[0].friendly = 1u; obs.squads[0].commandable = 1u; obs.squads[0].members = 4; obs.squads[0].lifeRatio = 1.0f;
    obs.takeableCount = 1u;
    obs.takeable[0].unitHandle = CeEntityHandlePack(4u, 1u);
    obs.takeable[0].spawnSeq = 1u; obs.takeable[0].lifeRatio = 1.0f;

    CeCommanderIntent cmd = {};
    CePossessIntent pos = {};
    CeAiTacticalL2Decide(&st, obs, nullptr, &cmd, &pos);
    if (cmd.valid != 0u || pos.valid != 0u) {
        std::puts("FAIL L2 acted while toggles were OFF"); return 21;
    }

    // Enable Commander + Play-Unit -> L2 MUST emit valid deterministic intents.
    obs.activeToggles |= CE_TOGGLE_COMMANDER | CE_TOGGLE_PLAY_UNIT;
    CeAiTacticalL2Decide(&st, obs, nullptr, &cmd, &pos);
    if (cmd.valid == 0u || cmd.verb != 2 || cmd.squadHandle != obs.squads[0].squadHandle) {
        std::puts("FAIL L2 did not issue advance order when Commander ON"); return 22;
    }
    if (pos.valid == 0u || pos.release != 0u || pos.unitHandle != obs.takeable[0].unitHandle) {
        std::puts("FAIL L2 did not take tank when Play-Unit ON"); return 23;
    }

    // Defend posture must NOT starve squad 1 once squad 0 already holds (verb == 3).
    obs.squadCount = 2u;
    obs.squads[0].verb = 3; // already CE_CMD_HOLD
    obs.squads[1].squadHandle = CeEntityHandlePack(6u, 1u);
    obs.squads[1].ordinal = 1;
    obs.squads[1].friendly = 1u;
    obs.squads[1].commandable = 1u;
    obs.squads[1].members = 4;
    obs.squads[1].lifeRatio = 1.0f;
    obs.squads[1].verb = 2; // currently advancing
    obs.simTick = 45u;
    CeStrategicPrior defPrior = {};
    defPrior.posture = CE_STRAT_DEFEND;
    CeAiTacticalL2Decide(&st, obs, &defPrior, &cmd, &pos);
    if (cmd.valid == 0u || cmd.verb != 3 || cmd.squadHandle != obs.squads[1].squadHandle) {
        std::puts("FAIL defendPosture starved squad 1 behind already-holding squad 0"); return 24;
    }
    // Once both squads hold, defendPosture must emit no redundant hold order.
    obs.squads[1].verb = 3;
    obs.simTick = 60u;
    CeAiTacticalL2Decide(&st, obs, &defPrior, &cmd, &pos);
    if (cmd.valid != 0u) {
        std::puts("FAIL defendPosture re-issued redundant hold to already-holding squads"); return 25;
    }
    return 0;
}

static int TestHeadless4SlotPossessionIsolation() {
    CePlayUnitSlotsCore core;
    CePlayUnitFacts tank = {};
    tank.friendly = true;
    tank.alive = true;
    tank.vehicle = true;
    tank.groundUnit = true;
    tank.normalSquad = true;
    tank.armed = true;

    const CeEntityHandle u1 = CeEntityHandlePack(11u, 1u);
    const CeEntityHandle u2 = CeEntityHandlePack(12u, 1u);
    const CeEntityHandle sq = CeEntityHandlePack(21u, 1u);
    const char *why = nullptr;

    // Refused when toggle OFF.
    if (core.Take(1u, 0, u1, sq, 1, tank, false, false, &why)) {
        std::puts("FAIL accepted take while toggle OFF"); return 30;
    }
    // Refused when human owns unit.
    if (core.Take(1u, 0, u1, sq, 1, tank, true, true, &why)) {
        std::puts("FAIL stole human-owned unit"); return 31;
    }
    // Refused for enemy unit.
    CePlayUnitFacts enemyTank = tank;
    enemyTank.friendly = false;
    if (core.Take(1u, 0, u1, sq, 1, enemyTank, true, false, &why)) {
        std::puts("FAIL took enemy unit"); return 32;
    }
    // Slot 1 takes u1 cleanly.
    if (!core.Take(1u, 0, u1, sq, 1, tank, true, false, &why)) {
        std::puts("FAIL slot 1 could not take eligible tank"); return 33;
    }
    // Slot 2 CANNOT steal u1 while slot 1 holds it!
    if (core.Take(2u, 0, u1, sq, 1, tank, true, false, &why)) {
        std::puts("FAIL slot 2 stole u1 from slot 1"); return 34;
    }
    // Slot 2 CAN take u2 concurrently (multi-slot isolation).
    if (!core.Take(2u, 1, u2, sq, 1, tank, true, false, &why)) {
        std::puts("FAIL slot 2 could not take independent tank u2"); return 35;
    }
    // Release slot 1 -> slot 2 still holds u2, and slot 3 can now acquire u1 cleanly.
    CeUnitPossessionSlot prev = {};
    if (!core.Release(1u, &prev) || prev.unitHandle != u1) {
        std::puts("FAIL release slot 1 failed"); return 36;
    }
    if (!core.IsUnitLeased(u2) || core.IsUnitLeased(u1)) {
        std::puts("FAIL lease table inconsistent after slot 1 release"); return 37;
    }
    if (!core.Take(3u, 0, u1, sq, 1, tank, true, false, &why)) {
        std::puts("FAIL slot 3 could not take released u1"); return 38;
    }
    // Determinism check on SplitMix64 seed.
    if (CePlayerAiSeed(0x12345678u, 2u, 42u) != CePlayerAiSeed(0x12345678u, 2u, 42u) ||
        CePlayerAiSeed(0x12345678u, 2u, 42u) == CePlayerAiSeed(0x12345678u, 3u, 42u)) {
        std::puts("FAIL CePlayerAiSeed determinism/salting"); return 39;
    }
    return 0;
}

static int TestNeutralAndHumanCommanderRespect() {
    // 07-P1-1: Neutral units and independent Repair Pads must NEVER be targeted as hostiles.
    CePlayerObs obs = {};
    obs.simTick = 30u;
    obs.slot = 2u;
    obs.lifeRatio = 1.0f;
    obs.pos[0] = 100.0f; obs.pos[1] = 100.0f; obs.pos[2] = -10.0f;
    obs.activeToggles = CE_TOGGLE_AI_PILOTS | CE_TOGGLE_COMMANDER | CE_TOGGLE_PLAY_UNIT;
    obs.entityCount = 2u;
    // Entity 0: independent Repair Pad (closer)
    obs.entities[0].isFriendly = 0u;
    obs.entities[0].isNeutral = 1u;
    obs.entities[0].isRepairPad = 1u;
    obs.entities[0].distSqQuantized = 400u;
    obs.entities[0].pos[0] = 120.0f; obs.entities[0].pos[1] = 110.0f;
    // Entity 1: neutral non-hostile unit
    obs.entities[1].isFriendly = 0u;
    obs.entities[1].isNeutral = 1u;
    obs.entities[1].isRepairPad = 0u;
    obs.entities[1].distSqQuantized = 600u;
    obs.entities[1].pos[0] = 130.0f; obs.entities[1].pos[1] = 120.0f;

    if (CePlayerObsEntityIsHostile(obs.entities[0]) || CePlayerObsEntityIsHostile(obs.entities[1])) {
        std::puts("FAIL neutral or independent repair pad flagged as hostile"); return 40;
    }
    if (!CePlayerObsEntityIsUsableRepairPad(obs.entities[0])) {
        std::puts("FAIL independent repair pad not recognized as usable repair pad"); return 41;
    }

    // L1 feature extractor must see independent Repair Pad and 0 hostiles.
    int16_t feat[kCeAiMlpInDim];
    CeAiPilotExtractFeaturesQ12(obs, nullptr, feat);
    if (feat[CE_AI_FEAT_HAS_HOSTILE] != 0) {
        std::puts("FAIL L1 treated neutral/independent repair pad as hostile"); return 42;
    }
    if (feat[CE_AI_FEAT_REPAIR_PAD_DX] == 0 && feat[CE_AI_FEAT_REPAIR_PAD_DY] == 0) {
        std::puts("FAIL L1 missed independent repair pad vector"); return 43;
    }

    // L2 must NOT issue attacks or take units when only neutral/repair pads exist.
    obs.squadCount = 1u;
    obs.squads[0].squadHandle = CeEntityHandlePack(3u, 1u);
    obs.squads[0].friendly = 1u; obs.squads[0].commandable = 1u; obs.squads[0].members = 4; obs.squads[0].lifeRatio = 1.0f;
    obs.takeableCount = 1u;
    obs.takeable[0].unitHandle = CeEntityHandlePack(4u, 1u);
    obs.takeable[0].spawnSeq = 1u; obs.takeable[0].lifeRatio = 1.0f;
    CeAiTacticalL2State st;
    CeAiTacticalL2Reset(&st);
    CeCommanderIntent cmd = {};
    CePossessIntent pos = {};
    obs.simTick = 24u; // (24 + 2*3) % 15 == 0
    CeAiTacticalL2Decide(&st, obs, nullptr, &cmd, &pos);
    if (cmd.valid != 0u || pos.valid != 0u) {
        std::puts("FAIL L2 assaulted neutral unit or independent repair pad"); return 44;
    }

    // Adding a real hostile further away must make L2 target the hostile, NOT the closer repair pad!
    obs.entityCount = 3u;
    obs.entities[2].isFriendly = 0u;
    obs.entities[2].isNeutral = 0u;
    obs.entities[2].isRepairPad = 0u;
    obs.entities[2].distSqQuantized = 2500u;
    obs.entities[2].pos[0] = 310.0f; obs.entities[2].pos[1] = 320.0f;
    CeAiTacticalL2Decide(&st, obs, nullptr, &cmd, &pos);
    if (cmd.valid == 0u || cmd.goalX != 310.0f || cmd.goalY != 320.0f) {
        std::puts("FAIL L2 did not target real hostile over closer repair pad"); return 45;
    }

    // 07-P1-2: Allied AI sharing a team with a human Commander (obs.teamHasHuman=1) must NEVER
    // overwrite human squad orders (not on HP < 30%, artillery deploy, or L3 DEFEND/REPAIR)
    // and must release/avoid hijacking units when Commander mode is enabled.
    CeAiTacticalL2Reset(&st);
    st.possessedUnit = obs.takeable[0].unitHandle;
    obs.teamHasHuman = 1u;
    obs.squadCount = 3u;
    obs.squads[0].lifeRatio = 0.20f; obs.squads[0].verb = 3; // low HP human-commanded HOLD
    obs.squads[1] = obs.squads[0];
    obs.squads[1].squadHandle = CeEntityHandlePack(5u, 1u);
    obs.squads[1].deploys = 1u; obs.squads[1].lifeRatio = 1.0f; obs.squads[1].verb = 2; // artillery
    obs.squads[2] = obs.squads[0];
    obs.squads[2].squadHandle = CeEntityHandlePack(6u, 1u);
    obs.squads[2].lifeRatio = 1.0f; obs.squads[2].verb = 0; // idle
    CeStrategicPrior defPrior = {};
    defPrior.posture = CE_STRAT_DEFEND;
    obs.simTick = 39u; // (39 + 2*3) % 15 == 0
    CeAiTacticalL2Decide(&st, obs, &defPrior, &cmd, &pos);
    if (cmd.valid != 0u) {
        std::puts("FAIL Allied AI overwrote human Commander team orders"); return 46;
    }
    if (pos.valid == 0u || pos.release != 1u) {
        std::puts("FAIL Allied AI did not release possessed unit on human Commander team"); return 47;
    }
    obs.simTick = 69u; // (69 + 2*3) % 15 == 0
    CeAiTacticalL2Decide(&st, obs, &defPrior, &cmd, &pos);
    if (cmd.valid != 0u || pos.valid != 0u) {
        std::puts("FAIL Allied AI hijacked unit or issued order on human Commander team"); return 48;
    }
    return 0;
}

int main() {
    int rc = 0;
    if ((rc = TestCrossLevelSpawnSeqSortInvariance()) != 0) return rc;
    if ((rc = TestFogOfWarParity()) != 0) return rc;
    if ((rc = TestToggleOffRefusalsAndL2Dormancy()) != 0) return rc;
    if ((rc = TestHeadless4SlotPossessionIsolation()) != 0) return rc;
    if ((rc = TestNeutralAndHumanCommanderRespect()) != 0) return rc;
    std::puts("PLAYER_HOOKS_OK");
    return 0;
}
'''


class PlayerHooksTests(unittest.TestCase):
    def compile_and_run(self, workdir: Path, header_overrides: dict[str, str] | None = None,
                        extra_flags: tuple[str, ...] = ()) -> tuple[int, str]:
        inc_dir = workdir / "inc"
        (inc_dir / "ai").mkdir(parents=True, exist_ok=True)
        for rel in ("ce_entity_handles.h", "ce_policy.h", "ce_modes.h", "ce_player_hooks.h",
                    "ce_playunit_rules.h", "ce_playunit_slots.h", "ai/ce_ai_pilot_l1.h", "ai/ce_ai_tactical_l2.h"):
            content = (GAME / rel).read_text(encoding="utf-8")
            if header_overrides and rel in header_overrides:
                content = header_overrides[rel]
            (inc_dir / rel).write_text(content, encoding="utf-8")
        l2_cpp = workdir / "ce_ai_tactical_l2.cpp"
        l2_text = (GAME / "ai/ce_ai_tactical_l2.cpp").read_text(encoding="utf-8")
        if header_overrides and "ai/ce_ai_tactical_l2.cpp" in header_overrides:
            l2_text = header_overrides["ai/ce_ai_tactical_l2.cpp"]
        l2_cpp.write_text(l2_text, encoding="utf-8")
        drv = workdir / "driver.cpp"
        drv.write_text(DRIVER_CPP, encoding="utf-8")
        exe = workdir / "driver"
        compiler = shutil.which("clang++") or shutil.which("g++")
        cmd = [compiler, "-std=gnu++14", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
               *extra_flags, "-I", str(inc_dir), str(drv), str(l2_cpp), "-o", str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode != 0:
            return -99, "COMPILE_FAIL:\n" + built.stdout + built.stderr
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        return ran.returncode, ran.stdout + ran.stderr

    def test_host_asan_ubsan_and_network_off_standalone(self):
        base = Path(tempfile.mkdtemp(prefix="run-", dir=SCRATCH))
        try:
            for label, flags in (("default", ()), ("network-off", ("-DCE_NETWORK_OFF=1",))):
                with self.subTest(flavour=label):
                    rc, out = self.compile_and_run(base / label, extra_flags=flags)
                    self.assertEqual(rc, 0, out)
                    self.assertIn("PLAYER_HOOKS_OK", out)
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_causal_mutants_are_caught(self):
        base = Path(tempfile.mkdtemp(prefix="mut-", dir=SCRATCH))
        try:
            ph = (GAME / "ce_player_hooks.h").read_text(encoding="utf-8")
            ps = (GAME / "ce_playunit_slots.h").read_text(encoding="utf-8")
            l2 = (GAME / "ai/ce_ai_tactical_l2.cpp").read_text(encoding="utf-8")
            mutants = {
                "sort-by-handle-bits": (
                    "ce_player_hooks.h",
                    ph.replace("key.spawnSeq < prev.spawnSeq", "key.handle < prev.handle"),
                ),
                "fog-of-war-removed": (
                    "ce_player_hooks.h",
                    ph.replace("return isFriendly || onScanner || (distSq <= 90.0f * 90.0f);",
                               "return isFriendly || onScanner || (distSq >= -1.0f);"),
                ),
                "sanitize-leaks-commander": (
                    "ce_player_hooks.h",
                    ph.replace("memset(&act->cmd, 0, sizeof(act->cmd));", "(void)act;"),
                ),
                "l2-ignores-toggles-off": (
                    "ai/ce_ai_tactical_l2.cpp",
                    l2.replace("const bool cmdOn = (obs.activeToggles & CE_TOGGLE_COMMANDER) != 0u;", "const bool cmdOn = true;"),
                ),
                "playunit-allows-double-lease": (
                    "ce_playunit_slots.h",
                    ps.replace("if (IsUnitLeased(unitHandle, &owner) && owner != (int)slot)", "if (false && IsUnitLeased(unitHandle, &owner) && owner != (int)slot)"),
                ),
                "playunit-ignores-toggle-off": (
                    "ce_playunit_slots.h",
                    ps.replace('if (!toggleAllowed) return refuse("play-unit toggle is off");', "(void)toggleAllowed;"),
                ),
            }
            for name, (rel, mutated) in mutants.items():
                with self.subTest(mutant=name):
                    orig = (GAME / rel).read_text(encoding="utf-8")
                    self.assertNotEqual(mutated, orig, f"mutant {name} did not change {rel}")
                    rc, out = self.compile_and_run(base / name, {rel: mutated})
                    self.assertNotIn("COMPILE_FAIL", out, f"mutant {name} failed to compile:\n{out}")
                    self.assertNotEqual(rc, 0, f"mutant {name} survived:\n{out}")
        finally:
            shutil.rmtree(base, ignore_errors=True)

    def test_static_invariants_and_patch_0396(self):
        def strip_comments(s: str) -> str:
            return re.sub(r"//[^\n]*|/\*[\s\S]*?\*/", "", s)

        ph = strip_comments((GAME / "ce_player_hooks.h").read_text(encoding="utf-8"))
        pcpp = strip_comments((GAME / "ce_player_hooks.cpp").read_text(encoding="utf-8"))
        cmd_cpp = strip_comments((GAME / "ce_commander.cpp").read_text(encoding="utf-8"))
        patch_0396 = (PORT / "patches/0396-ce-hook-player-act.patch").read_text(encoding="utf-8")

        # Network-neutral header: zero common.h and zero ce_network_*.h.
        self.assertNotIn("common.h", ph)
        self.assertNotIn("ce_network_", ph)
        self.assertNotIn("ce_online_", ph)

        # R12 & R2: ce_player_hooks.cpp must NEVER call GetCurrentWeapon() or CeEntityHandleFor() or .First()/.Next() on WORLD.
        self.assertNotIn("GetCurrentWeapon(", pcpp)
        self.assertNotIn("CeEntityHandleFor(", pcpp)
        self.assertNotIn("GetUnitNB().First(", pcpp)
        self.assertIn("CeEntityHandleLookup(", pcpp)
        self.assertIn("CeEntitySpawnSeqLookup(", pcpp)
        self.assertRegex(pcpp, r"CePlayerObs\s+gSlotObs\[CE_PLAYER_MAX_SLOTS\]")
        # Weapon range & live store telemetry + hostile building flag + possessedUnit reconciliation.
        self.assertIn("obs.weaponMinRange", pcpp)
        self.assertIn("obs.weaponMaxRange", pcpp)
        self.assertIn("BeStoreOverheatTag", pcpp)
        self.assertIn("e.isBaseBuilding = (u->IsA(THING_TYPE_BUILDING) != FALSE) ? 1u : 0u;", pcpp)
        self.assertIn("CePlayerObsEntityIsHostile(", pcpp)
        self.assertIn("CePlayUnitSlotRelease(", pcpp)
        self.assertIn("gTacticalState[slot].possessedUnit = (toggles & CE_TOGGLE_PLAY_UNIT)", pcpp)

        # Headless PlayUnit slots use per-unit PortCePlayUnitIsDriven, AI_ONF (armed), and ground-only driving.
        pus_cpp = strip_comments((GAME / "ce_playunit_slots.cpp").read_text(encoding="utf-8"))
        self.assertIn("PortCePlayUnitIsDriven", pus_cpp)
        self.assertIn("AI_ONF", pus_cpp)
        self.assertIn("IsEligibleGroundDriveClass(", pus_cpp)
        self.assertIn("IsEligibleGroundDriveUnit(", pus_cpp)
        self.assertNotIn("CePlayUnitOwnsPilot(", pus_cpp)

        # R5: 0396-ce-hook-player-act MUST gate on mGameState == GAME_STATE_PLAYING.
        self.assertIn("if (mGameState == GAME_STATE_PLAYING)", patch_0396)
        self.assertIn("ce_hook_player_act(CeHooksStepGameplayTick());", patch_0396)

        # Team-parameterized Commander port rejects cross-team with CE_WHY_ENEMY.
        self.assertIn("CeCmdListForTeam(", cmd_cpp)
        self.assertIn("CeCmdIssueForSlot(", cmd_cpp)
        self.assertIn("*outWhy = CE_WHY_ENEMY;", cmd_cpp)

        # SPDX GPL-3.0-only on every new Phase 3 file.
        for rel in ("ce_player_hooks.h", "ce_player_hooks.cpp", "ce_playunit_slots.h", "ce_playunit_slots.cpp",
                    "ai/ce_ai_pilot_l1.h", "ai/ce_ai_pilot_l1.cpp", "ai/ce_ai_tactical_l2.h", "ai/ce_ai_tactical_l2.cpp"):
            text = (GAME / rel).read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200], rel)


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
