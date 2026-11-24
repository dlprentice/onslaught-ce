#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clean-room Carrier Clash (F.S. Venturer vs. Fenrir) host verification suite.

Compiles a C++14 host driver with ASan/UBSan against:
  - port/game/ce_carrier_clash_world.h
  - port/game/ce_carrier_clash.h
  - port/game/ce_entity_handles.h / ce_entity_handles.cpp

Verifies:
  1. Procedural 512x512m maritime ocean arena heightfield (+/- 55 m clamp, deep
     central naval corridor, framing rocky sea-stacks, coastal launch bastion,
     exact 64x64 patch seam continuity, sun lightmap range).
  2. Clean-room version-50 base & real world serializer + round-trip parser
     (zero mission scripts, zero named meshes, explicit Fenrir Multiplayer
     rejection in every layer, 325-byte PMSH/PMS2 mesh stub builder).
  3. Strict preflight asset evaluator for every missing/corrupt retail asset.
  4. CeCarrierClashDirector state machine (32-bit CeEntityHandle flagship
     tracking past 65,536 generations, Victory/Defeat transitions and
     irreversibility, capped escort wave pacing, corridor patrol bounds).
  5. 9-slice HUD status strip layout geometry across aspect ratios & FONT_SMALL
     glyph safety (no [, ], |, _, etc.).
  6. Long-session soak (>65 minutes simulated time, >45,000 spawn/destroy cycles
     with live CeEntityHandleTable churn).
  7. 12 causal mutants across ce_carrier_clash_world.h and ce_carrier_clash.h.
  8. Patch 0377 CE_VERSION guard hygiene & forbidden-asset source hygiene.
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
PORT = ROOT / "port"
GAME = PORT / "game"
PATCH_0377 = PORT / "patches/0377-ce-carrier-clash.patch"

CXX = shutil.which("g++") or shutil.which("clang++") or "c++"

DRIVER_CPP = r'''
#include "ce_carrier_clash.h"
#include "ce_entity_handles.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int gFailures = 0;

static void Check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

static void TestHeightfieldAndSeams() {
    using namespace CeCarrierClashWorld;
    static int16_t samples[kHeightSamples];
    FillHeightSamples(samples);

    int16_t minRaw = 32767;
    int16_t maxRaw = -32768;
    int aboveWaterCount = 0;
    int deepChannelCount = 0;

    for (int i = 0; i < kHeightSamples; ++i) {
        if (samples[i] < minRaw) minRaw = samples[i];
        if (samples[i] > maxRaw) maxRaw = samples[i];
        const float z = static_cast<float>(samples[i]) * kHeightScale;
        Check(z >= kMinZBound && z <= kMaxZBound, "height sample within +/-55 m clamp");
        if (z < kWaterLevel - 1.0f) ++aboveWaterCount;
        if (z > kWaterLevel + 10.0f) ++deepChannelCount;
    }
    Check(minRaw >= -kMaxRawSample && maxRaw <= kMaxRawSample, "raw int16 within +/-28160");
    Check(aboveWaterCount > 2500, "framing rocky sea-stacks and islets rise above sea level");
    Check(deepChannelCount > 200000, "wide deep-water naval corridor below waterline");

    // Exact seam continuity across every adjacent 8x8 cell patch edge.
    for (int tx = 0; tx < kPatches; ++tx) {
        for (int ty = 0; ty < kPatches; ++ty) {
            const int16_t *p = samples + ((tx * kPatches + ty) * kPatchSamples * kPatchSamples);
            if (tx + 1 < kPatches) {
                const int16_t *east = samples + (((tx + 1) * kPatches + ty) * kPatchSamples * kPatchSamples);
                for (int sy = 0; sy < kPatchSamples; ++sy) {
                    if (p[sy * kPatchSamples + 8] != east[sy * kPatchSamples + 0]) {
                        Check(false, "east-west patch seam continuity");
                        return;
                    }
                }
            }
            if (ty + 1 < kPatches) {
                const int16_t *north = samples + ((tx * kPatches + (ty + 1)) * kPatchSamples * kPatchSamples);
                for (int sx = 0; sx < kPatchSamples; ++sx) {
                    if (p[8 * kPatchSamples + sx] != north[0 * kPatchSamples + sx]) {
                        Check(false, "north-south patch seam continuity");
                        return;
                    }
                }
            }
        }
    }

    // Central naval corridor stays deeply submerged so flagships never clip terrain.
    for (int y = 210; y <= 330; y += 15) {
        for (int x = 205; x <= 335; x += 15) {
            const float z = HeightAt(static_cast<float>(x), static_cast<float>(y));
            Check(z > kWaterLevel + 10.0f, "central flagship corridor stays deep water");
        }
    }

    // Coastal launch bastion forms a flat dry plateau at (kBastionX, kBastionY, kBastionTopZ).
    const float bastionCenter = HeightAt(kBastionX, kBastionY);
    const float bastionEdge = HeightAt(kBastionX + 5.0f, kBastionY - 4.0f);
    Check(std::fabs(bastionCenter - kBastionTopZ) < 0.05f, "bastion center at expected plateau height");
    Check(std::fabs(bastionEdge - kBastionTopZ) < 0.05f, "bastion inner plateau stays flat");
    Check(bastionCenter < kWaterLevel - 6.0f, "bastion well above waterline");

    // Out-of-bounds coordinates degrade safely without NaN/Inf.
    Check(HeightAt(-10.0f, 100.0f) == kDeepSeaFloorZ, "OOB height returns safe seabed");
    Check(HeightAt(600.0f, 100.0f) == kDeepSeaFloorZ, "OOB +X height returns safe seabed");

    // Directional sun lightmap stays inside MIXERMAP [14..62] byte band.
    uint8_t minLight = 255, maxLight = 0;
    for (int y = 0; y < kCells; y += 8) {
        for (int x = 0; x < kCells; x += 8) {
            const uint8_t l = ComputeCellLight(x, y, 0.55f, -0.35f, -0.76f);
            Check(l >= 14u && l <= 62u, "cell light within [14..62]");
            if (l < minLight) minLight = l;
            if (l > maxLight) maxLight = l;
        }
    }
    Check(maxLight > minLight + 10u, "sea-stacks produce directional sun shading contrast");
}

static void TestWorldSerializerAndForbiddenUnits() {
    using namespace CeCarrierClashWorld;

    // Explicit Fenrir Multiplayer refusal (exact, lowercase, mixed-case, empty, control char).
    Check(IsForbiddenUnit("Fenrir Multiplayer"), "exact Fenrir Multiplayer refused");
    Check(IsForbiddenUnit("fenrir multiplayer"), "lowercase Fenrir Multiplayer refused");
    Check(IsForbiddenUnit("FENRIR MULTIPLAYER"), "uppercase Fenrir Multiplayer refused");
    Check(IsForbiddenUnit(""), "empty unit name refused");
    Check(IsForbiddenUnit(nullptr), "null unit name refused");
    Check(IsForbiddenUnit("Bad\nName"), "control character unit name refused");
    Check(!IsForbiddenUnit(kVenturerUnit), "Aircarrier allowed");
    Check(!IsForbiddenUnit(kFenrirUnit), "Fenrir allowed");
    Check(!IsForbiddenUnit(kFenrirAltUnit), "Fenrir 2 allowed");

    UnitSpawnSpec oob[kMaxActors] = {};
    const unsigned count = BuildDefaultOrderOfBattle(oob, kMaxActors, false);
    Check(count == 11u, "default order of battle has 11 units");

    const WorldPlan plan = BuildDefaultWorldPlan(oob, count);
    Check(ValidateWorldPlan(plan), "default world plan valid");

    static uint8_t baseBuf[kMaxWorldBodyBytes];
    static uint8_t realBuf[kMaxWorldBodyBytes];
    unsigned baseLen = 0, realLen = 0;
    Check(WriteWorldBody(plan, false, baseBuf, sizeof(baseBuf), &baseLen), "write clean-room base world");
    Check(WriteWorldBody(plan, true, realBuf, sizeof(realBuf), &realLen), "write clean-room real world");
    Check(baseLen > 64u && realLen > baseLen, "real world larger than empty base world");

    WorldSummary baseSum = {}, realSum = {};
    Check(VerifyWorldBody(baseBuf, baseLen, &baseSum), "verify base world body");
    Check(baseSum.version == kWorldVersion && baseSum.baseLevel == -1 &&
          baseSum.scriptCount == 0u && baseSum.thingCount == 0u && baseSum.mapNumber == kMapId,
          "base world summary matches version 50 empty clean-room schema");

    Check(VerifyWorldBody(realBuf, realLen, &realSum), "verify real world body");
    Check(realSum.version == kWorldVersion && realSum.baseLevel == kBaseWorldId &&
          realSum.scriptCount == 0u && realSum.startCount == 1u &&
          realSum.unitCount == 11u && realSum.sawVenturer && realSum.sawFenrir &&
          !realSum.sawForbiddenUnit,
          "real world contains 1 CStart, Venturer, Fenrir, escorts, zero scripts, zero forbidden units");

    // Mutate order of battle to request Fenrir Multiplayer -> MUST fail validation & writer.
    oob[1].unitDef = kForbiddenFenrirUnit;
    const WorldPlan badPlan = BuildDefaultWorldPlan(oob, count);
    Check(!ValidateWorldPlan(badPlan), "world plan rejects Fenrir Multiplayer");
    unsigned badLen = 0;
    Check(!WriteWorldBody(badPlan, true, realBuf, sizeof(realBuf), &badLen),
          "WriteWorldBody refuses Fenrir Multiplayer");

    // Fenrir 2 alternate definition is explicitly valid.
    const unsigned altCount = BuildDefaultOrderOfBattle(oob, kMaxActors, true);
    Check(altCount == 11u, "alternate Fenrir 2 order of battle built");
    const WorldPlan altPlan = BuildDefaultWorldPlan(oob, altCount);
    Check(WriteWorldBody(altPlan, true, realBuf, sizeof(realBuf), &realLen), "write Fenrir 2 world");
    Check(VerifyWorldBody(realBuf, realLen, &realSum) && realSum.sawFenrir && !realSum.sawForbiddenUnit,
          "verify Fenrir 2 world");

    // Truncated world buffer fails verification cleanly.
    Check(!VerifyWorldBody(realBuf, 32u, &realSum), "truncated world rejected");

    // 325-byte PMSH/PMS2 mesh preload stub chunk layout.
    uint8_t stub[325] = {};
    unsigned stubLen = 0;
    Check(BuildMeshStubChunk("f_ventura.msh", 7701, true, stub, sizeof(stub), &stubLen),
          "build PMSH/PMS2 stub");
    Check(stubLen == 325u, "stub is exact 325 bytes");
    Check(std::memcmp(stub, "PMSH", 4) == 0 && std::memcmp(stub + 8, "PMS2", 4) == 0,
          "stub chunk headers PMSH + PMS2");
    uint32_t outerSize = 0, innerSize = 0;
    int32_t rid = 0;
    std::memcpy(&outerSize, stub + 4, 4);
    std::memcpy(&innerSize, stub + 12, 4);
    std::memcpy(&rid, stub + 316, 4);
    Check(outerSize == 317u && innerSize == 309u && rid == 7701 && stub[324] == 1u,
          "stub sizes, runtime_id and keep_buckets match CMesh::Deserialize");
    Check(std::strcmp(reinterpret_cast<const char *>(stub + 16), "f_ventura.msh") == 0,
          "stub carries mesh name");
    Check(!BuildMeshStubChunk("", 7701, true, stub, sizeof(stub), &stubLen),
          "empty mesh name refused");
    Check(!BuildMeshStubChunk("f_ventura.msh", 7701, true, stub, 100u, &stubLen),
          "undersized stub buffer refused");
}

static void TestPreflightMatrix() {
    using namespace CeCarrierClashWorld;
    PreflightInput good = {};
    good.archiveExists = true;
    good.archiveValid = true;
    for (unsigned i = 0; i < kPreloadMeshCount; ++i) good.meshesValid[i] = true;
    for (unsigned i = 0; i < kPreloadTextureCount; ++i) good.texturesValid[i] = true;
    for (unsigned i = 0; i < kRequiredUnitCount; ++i) good.unitsPresent[i] = true;
    good.requestedBossUnit = kFenrirUnit;

    Check(EvaluatePreflight(good).code == kPreflightOk, "complete retail inputs pass preflight");

    PreflightInput bad = good;
    bad.requestedBossUnit = kForbiddenFenrirUnit;
    Check(EvaluatePreflight(bad).code == kPreflightForbiddenUnit, "preflight refuses Fenrir Multiplayer");
    Check(EvaluatePreflight(bad).message[0] != '\0', "forbidden-unit refusal has player message");

    bad = good;
    bad.archiveExists = false;
    Check(EvaluatePreflight(bad).code == kPreflightMissingArchive, "missing 742 archive refused");

    bad = good;
    bad.archiveValid = false;
    Check(EvaluatePreflight(bad).code == kPreflightCorruptArchive, "corrupt 742 archive refused");

    for (unsigned i = 0; i < kPreloadMeshCount; ++i) {
        bad = good;
        bad.meshesValid[i] = false;
        Check(EvaluatePreflight(bad).code == kPreflightMissingMesh, "missing preload mesh refused");
    }
    for (unsigned i = 0; i < kPreloadTextureCount; ++i) {
        bad = good;
        bad.texturesValid[i] = false;
        Check(EvaluatePreflight(bad).code == kPreflightMissingMesh, "missing preload texture refused");
    }
    for (unsigned i = 0; i < kRequiredUnitCount; ++i) {
        bad = good;
        bad.unitsPresent[i] = false;
        Check(EvaluatePreflight(bad).code == kPreflightMissingDefinition, "missing unit def refused");
    }
}

static void TestDirectorLifecycleAndCaps() {
    CeCarrierClashDirector dir;
    Check(dir.Phase() == CE_CC_INACTIVE, "starts inactive");
    dir.Arm();
    Check(dir.Phase() == CE_CC_ARMED && dir.IsActiveOrArmed() && !dir.IsRunning(), "armed state");
    dir.BeginLevel();
    Check(dir.Phase() == CE_CC_ACTIVE && dir.IsRunning(), "active state");

    // 32-bit CeEntityHandle with high generation (> 65536) — proves zero 16-bit wrap truncation.
    const CeEntityHandle hVenturer = CeEntityHandlePack(7u, 90001u);
    const CeEntityHandle hFenrir = CeEntityHandlePack(19u, 120005u);
    const CeEntityHandle hStaleFenrir = CeEntityHandlePack(19u, 120004u);
    Check(CeEntityHandleGeneration(hVenturer) == 90001u, "32-bit handle preserves >16-bit generation");
    Check(hFenrir != hStaleFenrir, "different generations produce distinct handles");

    // Forbidden unit spawn is ignored cleanly.
    dir.OnUnitSpawn(CeEntityHandlePack(5u, 1u), "Fenrir Multiplayer",
                    CeCarrierClashWorld::kAllegianceMuspell, 5000.0f, 5000.0f);
    Check(!CeEntityHandleIsValid(dir.FenrirHandle()), "director ignores Fenrir Multiplayer spawn");

    dir.OnUnitSpawn(hVenturer, CeCarrierClashWorld::kVenturerUnit,
                    CeCarrierClashWorld::kAllegianceForseti, 12000.0f, 12000.0f);
    dir.OnUnitSpawn(hFenrir, CeCarrierClashWorld::kFenrirUnit,
                    CeCarrierClashWorld::kAllegianceMuspell, 15000.0f, 15000.0f);
    Check(dir.VenturerHandle() == hVenturer && dir.FenrirHandle() == hFenrir, "flagships tracked by 32-bit handle");

    // Destroying a stale generation handle on Fenrir's slot does NOT falsely trigger Victory!
    dir.OnUnitDestroyed(hStaleFenrir);
    Check(dir.Phase() == CE_CC_ACTIVE, "stale generation destroy ignored (zero ABA bug)");

    // Telemetry updates HUD percentages accurately (including 1% floor while >0 HP).
    dir.UpdateFlagshipTelemetry(6000.0f, 12000.0f, true, 10.0f, 15000.0f, true);
    CeCarrierClashHudState hud = dir.BuildHudState();
    Check(hud.venturerPct == 50 && hud.fenrirPct == 1, "HUD percent calculation + alive 1% floor");
    Check(CeCarrierClashIsFontSmallSafeText(hud.venturerText), "Venturer HUD label FONT_SMALL-safe");
    Check(CeCarrierClashIsFontSmallSafeText(hud.fenrirText), "Fenrir HUD label FONT_SMALL-safe");
    Check(CeCarrierClashIsFontSmallSafeText(hud.bannerText), "Banner text FONT_SMALL-safe");

    // First tick issues initial broadside patrol waypoints inside deep corridor.
    CeCarrierClashSpawnOrder spawns[4] = {};
    int spawnCount = 0;
    CeCarrierClashPatrolOrder vp = {}, fp = {};
    dir.Tick(0.05f, 15, spawns, 4, &spawnCount, &vp, &fp);
    Check(vp.valid && fp.valid, "initial patrol waypoints emitted");
    Check(CeCarrierClashWorld::HeightAt(vp.x, vp.y) > CeCarrierClashWorld::kWaterLevel + 10.0f,
          "Venturer patrol waypoint over deep water");
    Check(CeCarrierClashWorld::HeightAt(fp.x, fp.y) > CeCarrierClashWorld::kWaterLevel + 10.0f,
          "Fenrir patrol waypoint over deep water");

    // Advance past wave interval -> emits capped escort wave.
    for (int t = 0; t < 500; ++t) {
        dir.Tick(0.05f, 15, spawns, 4, &spawnCount, &vp, &fp);
        if (spawnCount > 0) break;
    }
    Check(spawnCount == 2, "wave spawns one Forseti and one Muspell fighter when under cap");
    for (int i = 0; i < spawnCount; ++i) {
        Check(!CeCarrierClashWorld::IsForbiddenUnit(spawns[i].unitName), "spawned escort allowed");
    }

    // Near mThingHeap ceiling (liveWorldUnits == maxWorldUnits - 1): zero spawns emitted!
    int capSpawns = 0;
    for (int t = 0; t < 600; ++t) {
        dir.Tick(0.05f, 95, spawns, 4, &spawnCount, &vp, &fp);
        capSpawns += spawnCount;
    }
    Check(capSpawns == 0, "mThingHeap safety cap blocks waves when world is near ceiling");

    // Destroy Fenrir -> Victory; subsequent Venturer loss does NOT overwrite Victory.
    dir.OnUnitDestroyed(hFenrir);
    Check(dir.Phase() == CE_CC_VICTORY, "destroying Fenrir triggers Victory");
    hud = dir.BuildHudState();
    Check(std::strstr(hud.bannerText, "VICTORY") != nullptr &&
          CeCarrierClashIsFontSmallSafeText(hud.bannerText), "Victory banner valid and safe");
    dir.OnUnitDestroyed(hVenturer);
    Check(dir.Phase() == CE_CC_VICTORY, "Victory state is irreversible");

    // Separate run: losing Venturer triggers Defeat; subsequent Fenrir loss does NOT overwrite Defeat.
    dir.BeginLevel();
    dir.OnUnitSpawn(hVenturer, CeCarrierClashWorld::kVenturerUnit,
                    CeCarrierClashWorld::kAllegianceForseti, 12000.0f, 12000.0f);
    dir.OnUnitSpawn(hFenrir, CeCarrierClashWorld::kFenrirUnit,
                    CeCarrierClashWorld::kAllegianceMuspell, 15000.0f, 15000.0f);
    dir.OnUnitDestroyed(hVenturer);
    Check(dir.Phase() == CE_CC_DEFEAT, "losing Venturer triggers Defeat");
    hud = dir.BuildHudState();
    Check(std::strstr(hud.bannerText, "VENTURER LOST") != nullptr &&
          CeCarrierClashIsFontSmallSafeText(hud.bannerText), "Venturer-lost Defeat banner valid");
    dir.OnUnitDestroyed(hFenrir);
    Check(dir.Phase() == CE_CC_DEFEAT, "Defeat state is irreversible");

    // Separate run: losing player Battle Engine triggers Defeat.
    dir.BeginLevel();
    dir.OnPlayerLost();
    Check(dir.Phase() == CE_CC_DEFEAT, "player loss triggers Defeat");
    hud = dir.BuildHudState();
    Check(std::strstr(hud.bannerText, "BATTLE ENGINE LOST") != nullptr &&
          CeCarrierClashIsFontSmallSafeText(hud.bannerText), "Player-lost Defeat banner valid");

    // EndLevel(keepArmed=true) keeps Retry armed; EndLevel(false) disarms cleanly.
    dir.EndLevel(true);
    Check(dir.Phase() == CE_CC_ARMED, "EndLevel(true) preserves armed state for Retry");
    dir.EndLevel(false);
    Check(dir.Phase() == CE_CC_INACTIVE, "EndLevel(false) disarms cleanly");
}

static void TestHudLayoutAndGlyphSafety() {
    // Reject forbidden FONT_SMALL glyphs explicitly.
    const char *forbidden = "[]|_~\\$@^{}";
    for (const char *p = forbidden; *p; ++p) {
        Check(!CeCarrierClashIsFontSmallSafeChar(*p), "forbidden FONT_SMALL char rejected");
    }
    Check(!CeCarrierClashIsFontSmallSafeText("VENTURER [100%]"), "bracketed text rejected");
    Check(!CeCarrierClashIsFontSmallSafeText("VENTURER | FENRIR"), "pipe text rejected");
    Check(CeCarrierClashIsFontSmallSafeText("F.S. VENTURER 100% - FENRIR 85%"), "clean HUD text accepted");

    char sanitized[64] = {};
    CeCarrierClashSanitizeHudText("F.S. [VENTURER] | 90%", sanitized, sizeof(sanitized));
    Check(CeCarrierClashIsFontSmallSafeText(sanitized), "sanitizer removes all forbidden glyphs");

    const float viewports[][3] = {
        {640.0f, 480.0f, 1.0f},
        {1280.0f, 720.0f, 1.5f},
        {1920.0f, 1080.0f, 2.25f},
        {2560.0f, 1080.0f, 2.25f},
    };
    for (unsigned i = 0; i < 4u; ++i) {
        const float w = viewports[i][0], h = viewports[i][1], s = viewports[i][2];
        const CeCarrierClashHudLayout l = CeCarrierClashComputeHudLayout(w, h, s, true);
        Check(l.valid, "HUD layout valid across viewports");
        Check(l.panel.x >= w * 0.03f && l.panel.x + l.panel.w <= w * 0.97f, "panel respects horizontal safe margin");
        Check(l.panel.y >= h * 0.03f && l.panel.y + l.panel.h < h * 0.30f, "panel sits cleanly in top strip");
        Check(l.venturerBar.x + l.venturerBar.w < l.fenrirBar.x, "Venturer and Fenrir bars never overlap");
        Check(std::fabs(l.venturerBar.w - l.fenrirBar.w) < 1.0f, "hull bars are mirrored equal width");
        Check(l.bannerBox.y > l.panel.y + l.panel.h, "outcome banner sits cleanly below top strip");
    }
    Check(!CeCarrierClashComputeHudLayout(100.0f, 100.0f, 1.0f, false).valid, "tiny viewport rejected");
}

static void TestLongSessionSoak() {
    // 66+ simulated minutes (>45,000 entity register/unregister cycles) exercising
    // CeEntityHandleTable 32-bit handles + CeCarrierClashDirector caps with zero leaks/ABA.
    static CeEntityHandleTable table;
    table.ClearAll();

    int dummyObjects[128] = {};
    const uintptr_t pVenturer = reinterpret_cast<uintptr_t>(&dummyObjects[0]);
    const uintptr_t pFenrir = reinterpret_cast<uintptr_t>(&dummyObjects[1]);
    CeEntityHandle venturerH = table.Register(pVenturer, 1u);
    CeEntityHandle fenrirH = table.Register(pFenrir, 2u);

    CeCarrierClashDirector dir;
    dir.Arm();
    dir.BeginLevel();
    dir.OnUnitSpawn(venturerH, CeCarrierClashWorld::kVenturerUnit,
                    CeCarrierClashWorld::kAllegianceForseti, 20000.0f, 20000.0f);
    dir.OnUnitSpawn(fenrirH, CeCarrierClashWorld::kFenrirUnit,
                    CeCarrierClashWorld::kAllegianceMuspell, 20000.0f, 20000.0f);

    CeEntityHandle activeEscorts[32] = {};
    int activeEscortCount = 0;
    int totalCycles = 0;

    // 80,000 ticks @ 0.05s = 4,000 simulated seconds (66.7 minutes)
    for (int tick = 0; tick < 80000; ++tick) {
        // Churn transient handles to force heavy slot reuse & high generations.
        if ((tick & 1) == 0) {
            const int slotObj = 2 + (tick % 10);
            const uintptr_t pTmp = reinterpret_cast<uintptr_t>(&dummyObjects[slotObj]);
            const CeEntityHandle tmp = table.Register(pTmp, static_cast<uint64_t>(tick + 10));
            Check(CeEntityHandleIsValid(tmp) && table.Resolve(tmp) == pTmp,
                  "soak transient register/resolve");
            table.Unregister(pTmp);
            Check(table.Resolve(tmp) == 0u, "soak unregistered handle resolves zero");
            ++totalCycles;
        }

        CeCarrierClashSpawnOrder spawns[4] = {};
        int spawnCount = 0;
        CeCarrierClashPatrolOrder vp = {}, fp = {};
        const int liveWorld = 6 + activeEscortCount;
        dir.Tick(0.05f, liveWorld, spawns, 4, &spawnCount, &vp, &fp);

        for (int s = 0; s < spawnCount; ++s) {
            Check(!CeCarrierClashWorld::IsForbiddenUnit(spawns[s].unitName), "soak spawn allowed");
            if (activeEscortCount < 32) {
                const int objIdx = 16 + activeEscortCount;
                const uintptr_t pEsc = reinterpret_cast<uintptr_t>(&dummyObjects[objIdx]);
                const CeEntityHandle eh = table.RegisterFresh(pEsc, static_cast<uint64_t>(totalCycles + 100));
                activeEscorts[activeEscortCount++] = eh;
                dir.OnUnitSpawn(eh, spawns[s].unitName, spawns[s].allegiance, 100.0f, 100.0f);
                ++totalCycles;
            }
        }

        // Periodically shoot down oldest escort so waves keep cycling for the whole hour.
        if ((tick % 180) == 179 && activeEscortCount > 0) {
            const CeEntityHandle dead = activeEscorts[0];
            const uintptr_t ptr = table.Resolve(dead);
            dir.OnUnitDestroyed(dead);
            if (ptr) table.Unregister(ptr);
            for (int k = 1; k < activeEscortCount; ++k) activeEscorts[k - 1] = activeEscorts[k];
            --activeEscortCount;
        }

        Check(dir.CountEscorts(CeCarrierClashWorld::kAllegianceForseti, true) <= 6,
              "soak Forseti fighter count never exceeds cap");
        Check(dir.CountEscorts(CeCarrierClashWorld::kAllegianceMuspell, true) <= 8,
              "soak Muspell fighter count never exceeds cap");
        Check(table.Resolve(venturerH) == pVenturer &&
              table.Resolve(fenrirH) == pFenrir,
              "flagship 32-bit handles remain intact throughout soak");
    }

    Check(totalCycles > 40000, "soak exercised >40,000 handle lifecycle cycles");
    Check(dir.WaveNumber() > 100, "soak completed >100 dynamic escort waves");
    Check(dir.Phase() == CE_CC_ACTIVE, "battle stays cleanly active until a flagship falls");

    dir.OnUnitDestroyed(fenrirH);
    Check(dir.Phase() == CE_CC_VICTORY, "final Fenrir kill ends soak cleanly in Victory");
}

int main() {
    TestHeightfieldAndSeams();
    TestWorldSerializerAndForbiddenUnits();
    TestPreflightMatrix();
    TestDirectorLifecycleAndCaps();
    TestHudLayoutAndGlyphSafety();
    TestLongSessionSoak();
    if (gFailures == 0) {
        std::puts("carrier_clash ok");
        return 0;
    }
    std::fprintf(stderr, "total failures: %d\n", gFailures);
    return 1;
}
'''


def build_and_run(include_dir: Path, work_dir: Path, asan: bool = True):
    driver_src = work_dir / "driver.cpp"
    driver_bin = work_dir / "driver"
    driver_src.write_text(DRIVER_CPP, encoding="utf-8")
    cmd = [
        CXX, "-std=c++14", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror",
        "-DCE_VERSION=\"test\"",
        "-I", str(include_dir),
        "-I", str(GAME),
        str(driver_src),
        "-o", str(driver_bin),
    ]
    if asan:
        cmd.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])
    comp = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if comp.returncode != 0:
        return False, "compile", comp.stdout + comp.stderr
    run = subprocess.run([str(driver_bin)], capture_output=True, text=True, timeout=60)
    ok = run.returncode == 0 and "carrier_clash ok" in run.stdout
    return ok, "run", run.stdout + run.stderr


class CarrierClashHostTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ce-carrier-clash-test-", dir=tempfile.gettempdir()))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_host_driver_asan_ubsan(self):
        ok, stage, out = build_and_run(GAME, self.tmp, asan=True)
        self.assertTrue(ok, f"ASan/UBSan host driver failed at {stage}:\n{out}")

    def test_causal_mutants_all_caught(self):
        world_src = (GAME / "ce_carrier_clash_world.h").read_text(encoding="utf-8")
        dir_src = (GAME / "ce_carrier_clash.h").read_text(encoding="utf-8")

        mutants = [
            # World heightfield, serializer, Fenrir Multiplayer refusal & preflight mutants
            ("allow-fenrir-multiplayer", "ce_carrier_clash_world.h",
             "if (StrEqualsIgnoreCase(unitName, kForbiddenFenrirUnit)) return true;",
             "if (false && StrEqualsIgnoreCase(unitName, kForbiddenFenrirUnit)) return true;"),
            ("break-height-clamp", "ce_carrier_clash_world.h",
             "if (z < kMinZBound + 1.0f) z = kMinZBound + 1.0f;",
             "if (z < -200.0f) z = -200.0f; z -= 80.0f;"),
            ("break-seam-continuity", "ce_carrier_clash_world.h",
             "const float wx = static_cast<float>(tx * kPatchCells + sx);",
             "const float wx = static_cast<float>(tx * kPatchCells + sx + (sx == 8 ? 3 : 0));"),
            ("wrong-world-version", "ce_carrier_clash_world.h",
             "w.u16(kWorldVersion);",
             "w.u16(49u);"),
            ("keep-buckets-off", "ce_carrier_clash_world.h",
             "outBuf[324] = keepBuckets ? 1u : 0u;",
             "outBuf[324] = 0u;"),
            ("skip-preflight-mesh-check", "ce_carrier_clash_world.h",
             "if (!in.meshesValid[i]) {",
             "if (false && !in.meshesValid[i]) {"),
            ("skip-preflight-unit-check", "ce_carrier_clash_world.h",
             "if (!in.unitsPresent[i]) {",
             "if (false && !in.unitsPresent[i]) {"),
            # Director, caps, victory/defeat, HUD/glyph mutants
            ("no-victory-on-fenrir-kill", "ce_carrier_clash.h",
             "if (fenrirSeen_ && !fenrirAlive_) {",
             "if (false && fenrirSeen_ && !fenrirAlive_) {"),
            ("no-defeat-on-venturer-loss", "ce_carrier_clash.h",
             "if (venturerSeen_ && !venturerAlive_) {",
             "if (false && venturerSeen_ && !venturerAlive_) {"),
            ("victory-overwritten-by-defeat", "ce_carrier_clash.h",
             "if (phase_ != CE_CC_ACTIVE) return;",
             "if (phase_ == CE_CC_INACTIVE) return;"),
            ("break-world-unit-cap", "ce_carrier_clash.h",
             "if (headroom >= 2) {",
             "if (true) { const int fakeHeadroom = 4; (void)fakeHeadroom;"),
            ("allow-forbidden-brackets", "ce_carrier_clash.h",
             "case '[': case ']': case '|': case '_': case '~':",
             "case '_': case '~':"),
        ]

        for name, filename, old, new in mutants:
            with self.subTest(mutant=name):
                src = world_src if filename == "ce_carrier_clash_world.h" else dir_src
                self.assertEqual(src.count(old), 1, f"mutant anchor mismatch for {name}")
                mdir = self.tmp / f"m-{name}"
                inc = mdir / "inc"
                inc.mkdir(parents=True)
                # Copy both headers into inc/ so #include "ce_carrier_clash_world.h" resolves locally.
                (inc / "ce_carrier_clash_world.h").write_text(
                    world_src.replace(old, new) if filename == "ce_carrier_clash_world.h" else world_src,
                    encoding="utf-8",
                )
                (inc / "ce_carrier_clash.h").write_text(
                    dir_src.replace(old, new) if filename == "ce_carrier_clash.h" else dir_src,
                    encoding="utf-8",
                )
                # Extra line fixup for break-world-unit-cap so emitted < headroom also bypasses cap
                if name == "break-world-unit-cap":
                    t = (inc / "ce_carrier_clash.h").read_text(encoding="utf-8")
                    t = t.replace("&& emitted < headroom", "")
                    (inc / "ce_carrier_clash.h").write_text(t, encoding="utf-8")
                ok, _, _ = build_and_run(inc, mdir, asan=False)
                self.assertFalse(ok, f"mutant {name} unexpectedly passed the driver")

    def test_patch_and_source_hygiene(self):
        self.assertTrue(PATCH_0377.exists(), "patch 0377-ce-carrier-clash.patch must exist")
        patch_text = PATCH_0377.read_text(encoding="latin-1")
        # Every added code line in 0377 must be inside #ifdef CE_VERSION (either
        # added by this hunk or within an existing CE_VERSION block in context).
        in_ce = 0
        hunk_has_existing_ce = False
        for lineno, line in enumerate(patch_text.splitlines(), 1):
            if line.startswith("@@"):
                in_ce = 0
                hunk_has_existing_ce = False
                continue
            if line.startswith(" ") and ("#ifdef CE_VERSION" in line or "ceMuseumBody" in line):
                hunk_has_existing_ce = True
                continue
            if not line.startswith("+") or line.startswith("+++"):
                continue
            code = line[1:].strip()
            if code.startswith("#ifdef CE_VERSION"):
                in_ce += 1
                continue
            if code.startswith("#endif") and in_ce > 0:
                in_ce -= 1
                continue
            if code.startswith("#else"):
                continue
            if not code or code.startswith("//"):
                continue
            self.assertTrue(in_ce > 0 or hunk_has_existing_ce,
                            f"0377 line {lineno} not guarded by #ifdef CE_VERSION: {code}")

        # SPDX header on every new Carrier Clash file.
        for path in (
            GAME / "ce_carrier_clash_world.h",
            GAME / "ce_carrier_clash_world.cpp",
            GAME / "ce_carrier_clash.h",
            GAME / "ce_carrier_clash.cpp",
            Path(__file__),
        ):
            head = path.read_text(encoding="utf-8")[:200]
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", head, f"missing SPDX in {path.name}")

        # Verify transition, mutual-exclusion, localized defeat reason, HUD batching, and leak guards.
        cc_cpp = (GAME / "ce_carrier_clash.cpp").read_text(encoding="utf-8")
        cc_world_cpp = (GAME / "ce_carrier_clash_world.cpp").read_text(encoding="utf-8")
        self.assertIn("PortCeLivingSandboxActive", cc_cpp)
        self.assertIn("PortCePlayUnitShowsBattleline", cc_cpp)
        self.assertIn("UpdateP0PlayUnitStasis()", cc_cpp)
        self.assertIn("534802956", cc_cpp)
        self.assertNotIn("DeclareLevelLost(0,", cc_cpp)
        self.assertIn("GAMEINTERFACE.IsMenuUp()", cc_cpp)
        self.assertIn("GAME.GetGameState() != GAME_STATE_PLAYING", cc_cpp)
        self.assertIn("D3DPT_TRIANGLELIST", cc_cpp)
        self.assertNotIn("D3DPT_TRIANGLEFAN", cc_cpp)
        self.assertNotIn("D3DPT_TRIANGLESTRIP", cc_cpp)
        self.assertIn("RefreshProceduralMapExtents()", cc_world_cpp)
        self.assertIn("EngineMediaAccess::KempyCube()", cc_world_cpp)
        self.assertIn("EngineMediaAccess::Water()", cc_world_cpp)
        self.assertIn("gWorld.arenaInstalled", cc_world_cpp)
        self.assertIn("CMesh::GetMeshByResourceID(", cc_world_cpp)


if __name__ == "__main__":
    unittest.main()
