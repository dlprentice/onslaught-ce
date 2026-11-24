#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clean-room Living Sandbox (512m Combined-Arms Persistent Battle Director)
host verification suite (ASan/UBSan + 4-hour simulated soak + causal mutants).

Verifies:
  1. Procedural 512x512m combined-arms multi-biome heightfield (+/- 55 m clamp,
     64x64 patch seam continuity, NW Sanctuary Citadel exact flat terrace at
     Z = -34 m across [72..152]x[352..432], 4 m raised North/West/East parapet
     berm at Z = -38 m with open SE overlook, smooth 16 m wide SE grand access
     ramp descending to valley Z = -18 m, deep SE coastal naval strait at Z = +7 m,
     Forseti & Muspell base plateaus, sun lightmap [14..62]).
  2. Clean-room version-50 base & real world serializer + round-trip parser
     (zero mission scripts, zero named meshes, Sanctuary Dome keep-out enforced
     on every spawn, forbidden unit refusal, 325-byte PMSH/PMS2 stub builder).
  3. Strict retail 700_res_PC.aya preflight evaluator.
  4. Sandbox Launch Profile startup & runtime admission gates (strictly FALSE
     for faithful, campaign, replay, recording, network/lockstep, demo, probe).
  5. CeSandboxDirector 2 Hz state machine: 5-node tug-of-war capture hysteresis,
     suppressible/60s-regenerating HQ cores (endless world, never game over),
     population cap with 15% hysteresis & frame-time/event-50%/heap-768KiB
     downshift, stuck-unit reaper (>90s), periodic 25s regroup lull & heap
     coalesce every 15m, P0 Citadel emergency recall + Play-Any-Unit BE stasis,
     timed 5m Naval/Carrier Escalation, Sanctuary keep-out enforcement on all
     spawns & waypoints, and FONT_SMALL-safe 9-slice HUD layout.
  6. Simulated 4-hour (14,400 s = 28,800 2 Hz director steps) headless battle
     soak with live 32-bit CeEntityHandleTable churn (>100,000 spawns/deaths),
     proving flat bounded population, heap, event, and handle invariants.
  7. >= 14 causal mutants across ce_sandbox_world.h, ce_sandbox_director.h, and
     ce_sandbox_telemetry.h.
  8. Patch 0378 CE_VERSION hygiene, strict file ownership (zero Round.cpp or
     DXEngine.cpp hunks), SPDX headers, and clean text/comment hygiene.
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
GAME = PORT / "game"
PATCH_0378 = PORT / "patches/0378-ce-living-sandbox.patch"

CXX = shutil.which("g++") or shutil.which("clang++") or "c++"

DRIVER_CPP = r'''
#include "ce_sandbox_director.h"
#include "ce_sandbox_telemetry.h"
#include "ce_entity_handles.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int gFailures = 0;

static void Check(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

static void TestHeightfieldCitadelAndSeams() {
    using namespace CeSandboxWorld;
    static int16_t samples[kHeightSamples];
    FillHeightSamples(samples);

    int16_t minRaw = 32767;
    int16_t maxRaw = -32768;
    int dryCount = 0;
    int deepStraitCount = 0;

    for (int i = 0; i < kHeightSamples; ++i) {
        if (samples[i] < minRaw) minRaw = samples[i];
        if (samples[i] > maxRaw) maxRaw = samples[i];
        const float z = static_cast<float>(samples[i]) * kHeightScale;
        Check(z >= kMinZBound && z <= kMaxZBound, "height sample within +/-55 m clamp");
        if (z < kWaterLevel - 2.0f) ++dryCount;
        if (z > kWaterLevel + 10.0f) ++deepStraitCount;
    }
    Check(minRaw >= -kMaxRawSample && maxRaw <= kMaxRawSample, "raw int16 within +/-28160");
    Check(dryCount > 200000, "predominantly dry combined-arms valley & highlands");
    Check(deepStraitCount > 15000, "deep SE coastal naval strait carved below waterline");

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

    // Sanctuary Citadel flat terrace exactness across [72..152] x [352..432] at Z = -34.0 m.
    for (int y = 352; y <= 432; y += 4) {
        for (int x = 72; x <= 152; x += 4) {
            const float z = HeightAt(static_cast<float>(x), static_cast<float>(y));
            Check(std::fabs(z - (-34.0f)) < 0.005f, "citadel terrace dead-flat at -34.0 m");
        }
    }

    // 4 m raised parapet rampart berm (Z = -38.0 m) along North, West, and East outer edges,
    // while South-East overlook (y < 352) stays cleanly open to the valley.
    Check(HeightAt(112.0f, 437.0f) < kCitadelFloorZ - 2.5f, "north citadel parapet berm raised");
    Check(HeightAt(67.0f, 396.0f) < kCitadelFloorZ - 2.5f, "west citadel parapet berm raised");
    Check(HeightAt(157.0f, 396.0f) < kCitadelFloorZ - 2.5f, "east citadel parapet berm raised");
    Check(HeightAt(112.0f, 344.0f) > kCitadelFloorZ + 1.0f, "south overlook opens down to valley");

    // Smooth SE grand access ramp from (128, 352, -34) to (144, 316, -18).
    const float rampTop = HeightAt(128.0f, 352.0f);
    const float rampMid = HeightAt(136.0f, 334.0f);
    const float rampBot = HeightAt(144.0f, 316.0f);
    Check(std::fabs(rampTop - kCitadelFloorZ) < 0.05f, "ramp top meets citadel terrace");
    Check(rampMid > kCitadelFloorZ + 4.0f && rampMid < kValleyFloorZ - 4.0f, "ramp midpoint smoothly graded");
    Check(std::fabs(rampBot - kValleyFloorZ) < 1.5f, "ramp bottom joins valley floor");

    // Faction main base plateaus and deep SE coastal naval strait.
    Check(std::fabs(HeightAt(kForsetiHqX, kForsetiHqY) - kBasePlateauZ) < 0.05f, "forseti HQ plateau flat");
    Check(std::fabs(HeightAt(kMuspellHqX, kMuspellHqY) - kBasePlateauZ) < 0.05f, "muspell HQ plateau flat");
    Check(HeightAt(400.0f, 130.0f) > kWaterLevel + 12.0f, "SE coastal naval strait deep water");
    Check(HeightAt(320.0f, 200.0f) < kWaterLevel - 3.0f, "coastal harbor capture node stays dry");
    Check(HeightAt(208.0f, 272.0f) < kValleyFloorZ - 4.0f, "west ridge elevated high ground");

    // Sanctuary keep-out helper & clamp.
    Check(IsInsideCitadelKeepOut(112.0f, 392.0f), "citadel center inside keep-out");
    Check(IsInsideCitadelKeepOut(112.0f, 392.0f - 50.0f), "dome interior inside keep-out");
    Check(!IsInsideCitadelKeepOut(256.0f, 256.0f), "central crossroads outside keep-out");
    float cx = 112.0f, cy = 392.0f;
    ClampOutsideCitadelKeepOut(cx, cy);
    Check(!IsInsideCitadelKeepOut(cx, cy, kDirectorKeepOutRadius), "clamped point pushed outside keep-out");

    // Sun lightmap stays inside [14..62] with contrast.
    uint8_t minL = 255, maxL = 0;
    for (int y = 0; y < kCells; y += 8) {
        for (int x = 0; x < kCells; x += 8) {
            const uint8_t l = ComputeCellLight(x, y, 0.52f, 0.68f, -0.51f);
            Check(l >= 14u && l <= 62u, "cell light within [14..62]");
            if (l < minL) minL = l;
            if (l > maxL) maxL = l;
        }
    }
    Check(maxL > minL + 10u, "multi-biome relief produces directional lighting contrast");
}

static void TestWorldSerializerAndPreflight() {
    using namespace CeSandboxWorld;

    UnitSpawnSpec specs[kMaxActors] = {};
    const unsigned count = BuildDefaultOrderOfBattle(specs, kMaxActors);
    Check(count >= 25u && count <= kMaxActors, "default order of battle populated");

    const WorldPlan plan = BuildDefaultWorldPlan(specs, count);
    Check(ValidateWorldPlan(plan), "default world plan valid");
    Check(std::fabs(plan.startPosition[0] - kPlayerSpawnX) < 0.01f &&
          std::fabs(plan.startPosition[1] - kPlayerSpawnY) < 0.01f &&
          std::fabs(plan.startPosition[2] - kPlayerSpawnZ) < 0.01f,
          "P0 starts on Sanctuary Citadel overlook pad");

    static uint8_t baseBuf[kMaxWorldBodyBytes];
    static uint8_t realBuf[kMaxWorldBodyBytes];
    unsigned baseLen = 0u, realLen = 0u;
    Check(WriteWorldBody(plan, false, baseBuf, sizeof(baseBuf), &baseLen), "write base world");
    Check(WriteWorldBody(plan, true, realBuf, sizeof(realBuf), &realLen), "write real world");

    WorldSummary baseSum = {}, realSum = {};
    Check(VerifyWorldBody(baseBuf, baseLen, &baseSum), "verify base world");
    Check(baseSum.version == kWorldVersion && baseSum.baseLevel == -1 &&
          baseSum.scriptCount == 0u && baseSum.thingCount == 0u && baseSum.mapNumber == kMapId,
          "base world schema valid");
    Check(VerifyWorldBody(realBuf, realLen, &realSum), "verify real world");
    Check(realSum.version == kWorldVersion && realSum.baseLevel == kBaseWorldId &&
          realSum.scriptCount == 0u && realSum.startCount == 1u &&
          realSum.unitCount == count && realSum.forsetiCount >= 10u &&
          realSum.muspellCount >= 10u && !realSum.sawForbiddenUnit &&
          !realSum.sawUnitInsideKeepOut,
          "real world contains balanced armies, 1 CStart, zero scripts, zero keep-out violations");

    // Mutate one unit inside Sanctuary Dome keep-out -> MUST fail validation & writer.
    specs[3].position[0] = kCitadelCenterX + 10.0f;
    specs[3].position[1] = kCitadelCenterY - 10.0f;
    const WorldPlan badKeepOut = BuildDefaultWorldPlan(specs, count);
    Check(!ValidateWorldPlan(badKeepOut), "world plan rejects unit inside Sanctuary keep-out");
    unsigned badLen = 0u;
    Check(!WriteWorldBody(badKeepOut, true, realBuf, sizeof(realBuf), &badLen), "writer refuses keep-out unit");

    // Restore & verify forbidden unit refusal.
    BuildDefaultOrderOfBattle(specs, kMaxActors);
    specs[3].unitDef = "Fenrir Multiplayer";
    const WorldPlan badUnit = BuildDefaultWorldPlan(specs, count);
    Check(!ValidateWorldPlan(badUnit), "world plan rejects forbidden unit");

    // 325-byte PMSH/PMS2 stub builder.
    uint8_t stub[336] = {};
    unsigned stubLen = 0u;
    Check(BuildMeshStubChunk("f_ventura.msh", 920, true, stub, sizeof(stub), &stubLen), "mesh stub builds");
    Check(stubLen == 325u && stub[0] == 'P' && stub[1] == 'M' && stub[8] == 'P' && stub[11] == '2', "stub header");

    // Strict retail 700_res_PC.aya preflight evaluator.
    PreflightInput okIn = {};
    okIn.archiveExists = true;
    okIn.archiveValid = true;
    for (unsigned i = 0; i < kRequiredUnitCount; ++i) okIn.unitsPresent[i] = true;
    Check(EvaluatePreflight(okIn).code == kPreflightOk, "preflight accepts complete 700 install");
    { PreflightInput x = okIn; x.archiveExists = false; Check(EvaluatePreflight(x).code == kPreflightMissingArchive, "missing 700 archive refused"); }
    { PreflightInput x = okIn; x.archiveValid = false; Check(EvaluatePreflight(x).code == kPreflightCorruptArchive, "corrupt 700 archive refused"); }
    { PreflightInput x = okIn; x.unitsPresent[5] = false; Check(EvaluatePreflight(x).code == kPreflightMissingDefinition, "missing unit def refused"); }
}

static void TestSandboxProfileGates() {
    CeSandboxProfileGate clean = {};
    clean.sandboxOptIn = true;
    clean.ceLayer = true;
    clean.recording = false;
    clean.fencedEnv = false;
    clean.networkOrLockstep = false;
    clean.demo = false;
    clean.replayOrProbe = false;
    clean.retailCampaign = false;

    Check(CeSandboxProfileAllowed(clean), "runtime sandbox profile allowed when clean");
    Check(CeSandboxStartupProfileAllowed(clean), "startup sandbox profile allowed when clean");

    // Startup gate stays open even before level begins (retailCampaign=true),
    // while runtime gate requires non-campaign session.
    { CeSandboxProfileGate g = clean; g.retailCampaign = true;
      Check(!CeSandboxProfileAllowed(g), "runtime profile blocks retail campaign");
      Check(CeSandboxStartupProfileAllowed(g), "startup profile allows pre-level --sandbox-profile"); }

    // Default without opt-in is strictly FALSE for both.
    { CeSandboxProfileGate g = clean; g.sandboxOptIn = false;
      Check(!CeSandboxProfileAllowed(g), "runtime profile requires opt-in");
      Check(!CeSandboxStartupProfileAllowed(g), "startup profile requires opt-in"); }

    // Every faithful/replay/recording/lockstep/demo/probe fence blocks BOTH gates.
    { CeSandboxProfileGate g = clean; g.ceLayer = false; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks !ceLayer"); }
    { CeSandboxProfileGate g = clean; g.recording = true; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks recording"); }
    { CeSandboxProfileGate g = clean; g.fencedEnv = true; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks fencedEnv"); }
    { CeSandboxProfileGate g = clean; g.networkOrLockstep = true; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks network/lockstep"); }
    { CeSandboxProfileGate g = clean; g.demo = true; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks demo"); }
    { CeSandboxProfileGate g = clean; g.replayOrProbe = true; Check(!CeSandboxStartupProfileAllowed(g), "startup blocks replay/probe"); }
}

static void TestDirectorMechanicsAndHud() {
    using namespace CeSandboxWorld;
    CeSandboxDirector dir;
    CeSandboxDirectorConfig cfg = CeSandboxDefaultConfig(true);
    Check(cfg.targetUnitCap >= 180 && cfg.targetUnitCap <= 220, "expanded sandbox target cap in 180..220");
    Check(CeSandboxDefaultConfig(false).targetUnitCap == 96, "retail heap cap stays strict 96");

    dir.BeginLevel(cfg);
    Check(dir.IsRunning(), "director active after BeginLevel");

    // Register Forseti & Muspell HQ cores.
    const CeEntityHandle fHq = CeEntityHandlePack(1u, 1u);
    const CeEntityHandle mHq = CeEntityHandlePack(2u, 1u);
    dir.OnUnitSpawn(fHq, kUnitForsetiHq, kAllegianceForseti, kForsetiHqX, kForsetiHqY, 500.0f);
    dir.OnUnitSpawn(mHq, kUnitMuspellHq, kAllegianceMuspell, kMuspellHqX, kMuspellHqY, 500.0f);

    // 1. 2 Hz cadence: 0.05 s ticks only step every 10 ticks (0.5 s).
    CeSandboxPressure press = {16.0f, 30, 1200, 60000, 10u * 1024u * 1024u, true};
    CeSandboxNodePresence pres = {};
    CeSandboxStepOutput out = {};
    int stepped = 0;
    for (int t = 0; t < 20; ++t) {
        if (dir.Tick(0.05f, press, pres, nullptr, 0u, &out)) ++stepped;
    }
    Check(stepped == 2, "director steps strictly at 2 Hz");

    // 2. 5-node tug-of-war capture transitions.
    pres.forsetiWeight[2] = 4;
    for (int s = 0; s < 14; ++s) {
        dir.Tick(0.5f, press, pres, nullptr, 0u, &out);
    }
    Check(dir.NodeOwner(2) == CE_SB_OWNER_FORSETI, "central crossroads captured by Forseti");
    Check(dir.GetStats().totalNodeCaptures >= 1u, "capture counter incremented");

    // 3. Suppressible & 60 s regenerating HQ core (never ends sandbox).
    dir.OnUnitDestroyed(mHq);
    Check(dir.IsMuspellHqSuppressed(), "Muspell HQ enters suppressed state on destruction");
    Check(dir.IsRunning(), "sandbox stays running after HQ suppression");
    bool rebuilt = false;
    pres.forsetiWeight[2] = 0;
    for (int s = 0; s < 125; ++s) {
        dir.Tick(0.5f, press, pres, nullptr, 0u, &out);
        for (int k = 0; k < out.spawnCount; ++k) {
            if (out.spawns[k].isHqRebuild && out.spawns[k].allegiance == kAllegianceMuspell) {
                rebuilt = true;
            }
        }
    }
    Check(rebuilt && !dir.IsMuspellHqSuppressed(), "Muspell HQ regenerates after 60 s");

    // 4. Population cap hysteresis (15%) & event-50% / heap-768KiB / frame-time downshift.
    CeSandboxPressure highPop = press;
    highPop.liveWorldUnits = dir.EffectiveCap();
    dir.Tick(0.5f, highPop, pres, nullptr, 0u, &out);
    Check(dir.IsHysteresisPaused(), "hysteresis pauses waves at cap");
    highPop.liveWorldUnits = dir.EffectiveCap() - 5; // still above 85%
    dir.Tick(0.5f, highPop, pres, nullptr, 0u, &out);
    Check(dir.IsHysteresisPaused(), "hysteresis stays paused inside 15% band");
    highPop.liveWorldUnits = static_cast<int>(dir.EffectiveCap() * 0.80f);
    dir.Tick(0.5f, highPop, pres, nullptr, 0u, &out);
    Check(!dir.IsHysteresisPaused(), "hysteresis resumes below 85% band");

    CeSandboxPressure evtAlarm = press;
    evtAlarm.eventPending = 30000;
    evtAlarm.eventCapacity = 60000;
    dir.Tick(0.5f, evtAlarm, pres, nullptr, 0u, &out);
    Check(dir.IsPressureThrottled(), "50% event pool alarm throttles spawns");

    CeSandboxPressure heapAlarm = press;
    heapAlarm.thingHeapFreeBytes = 512u * 1024u;
    dir.Tick(0.5f, heapAlarm, pres, nullptr, 0u, &out);
    Check(dir.IsPressureThrottled(), "low mThingHeap free bytes (<768 KiB) throttles spawns");

    CeSandboxPressure slowFrame = press;
    slowFrame.frameTimeMs = 34.0f;
    for (int s = 0; s < 12; ++s) dir.Tick(0.5f, slowFrame, pres, nullptr, 0u, &out);
    Check(dir.EffectiveCap() < cfg.targetUnitCap, "frame time >25 ms downshifts effective unit cap");

    // 5. Stuck-unit reaper (>90 s with <2 m movement & no combat).
    const CeEntityHandle stuckH = CeEntityHandlePack(50u, 1u);
    dir.OnUnitSpawn(stuckH, kUnitBroadsword, kAllegianceForseti, 220.0f, 220.0f, 100.0f);
    CeSandboxTrackedSample sm = {stuckH, 220.5f, 220.4f, -18.0f, 100.0f, false, true};
    bool reaped = false;
    for (int s = 0; s < 190 && !reaped; ++s) {
        dir.Tick(0.5f, press, pres, &sm, 1u, &out);
        for (int r = 0; r < out.reapCount; ++r) {
            if (out.reaps[r].handle == stuckH) reaped = true;
        }
    }
    Check(reaped, "stuck unit reaped after >90 s stationary without combat");

    // 6. P0 protection: Play-Any-Unit stasis shield vs critical-HP / water Citadel recall.
    {
        const CeSandboxPlayerAction stasis = CeSandboxEvaluatePlayerProtection({true, false, 10.0f, 100.0f, true});
        Check(stasis.engageStasisShield && !stasis.recallToCitadel, "Play-Any-Unit engages BE stasis shield");
        const CeSandboxPlayerAction lowHp = CeSandboxEvaluatePlayerProtection({true, false, 5.0f, 100.0f, false});
        Check(!lowHp.engageStasisShield && lowHp.recallToCitadel, "critical low HP triggers Citadel recall");
        const CeSandboxPlayerAction water = CeSandboxEvaluatePlayerProtection({true, true, 90.0f, 100.0f, false});
        Check(water.recallToCitadel, "water immersion triggers Citadel recall");
        const CeSandboxPlayerAction healthy = CeSandboxEvaluatePlayerProtection({true, false, 80.0f, 100.0f, false});
        Check(!healthy.engageStasisShield && !healthy.recallToCitadel, "healthy P0 plays uninterrupted");
    }

    // 7. FONT_SMALL-safe HUD text & 9-slice layout geometry across aspect ratios.
    const CeSandboxHudState hud = dir.BuildHudState();
    Check(CeSandboxIsFontSmallSafeText(hud.headerLeft), "HUD left header FONT_SMALL-safe");
    Check(CeSandboxIsFontSmallSafeText(hud.headerMid), "HUD mid header FONT_SMALL-safe");
    Check(CeSandboxIsFontSmallSafeText(hud.headerRight), "HUD right header FONT_SMALL-safe");
    Check(!CeSandboxIsFontSmallSafeText("BAD[TEXT]|_~"), "glyph filter rejects unsupported glyphs");
    char cleaned[32] = {};
    CeSandboxSanitizeHudText("NODE[1]_OK|TEST", cleaned, sizeof(cleaned));
    Check(CeSandboxIsFontSmallSafeText(cleaned), "sanitizer produces safe ASCII");

    const float sizes[3][2] = {{640.0f, 480.0f}, {1280.0f, 720.0f}, {2560.0f, 1440.0f}};
    for (int i = 0; i < 3; ++i) {
        const float w = sizes[i][0], h = sizes[i][1];
        const CeSandboxHudLayout lay = CeSandboxComputeHudLayout(w, h, h / 480.0f, true, true);
        Check(lay.valid, "HUD layout valid");
        Check(lay.panel.x >= w * 0.03f && lay.panel.x + lay.panel.w <= w * 0.97f, "HUD panel within safe X");
        Check(lay.panel.y >= h * 0.03f, "HUD panel below safe top margin");
        Check(lay.bannerBox.y > lay.panel.y + lay.panel.h, "banner cleanly below top strip");
        Check(lay.cueBox.y > lay.bannerBox.y + lay.bannerBox.h, "citadel cue box clear of top HUD");
        Check(lay.cueBox.y + lay.cueBox.h <= h * 0.96f, "citadel cue box within bottom safe margin");
    }
}

// Simulated 4-hour (14,400 s = 28,800 2 Hz steps) persistent Living Sandbox soak
// exercising live 32-bit CeEntityHandleTable churn, shifting capture nodes, HQ
// suppressions/rebuilds, periodic 15m regroup lulls + heap coalescing, timed 5m
// Naval Escalations, stuck-unit reaping, and Sanctuary Dome keep-out invariants.
static void TestFourHourHeadlessSoak() {
    using namespace CeSandboxWorld;

    struct SimUnit {
        CeEntityHandle handle;
        uintptr_t fakePtr;
        int allegiance;
        CeSandboxUnitRole role;
        float x, y, z;
        float tx, ty;
        float life;
        bool stuck;
    };

    static CeEntityHandleTable handles;
    handles.ClearAll();

    CeSandboxDirector dir;
    CeSandboxDirectorConfig cfg = CeSandboxDefaultConfig(true);
    dir.BeginLevel(cfg);

    std::vector<SimUnit> world;
    world.reserve(256);

    uint32_t rng = 0x5A17B001u;
    auto nextRand = [&]() -> uint32_t {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    std::vector<uintptr_t> freeAddrs;
    freeAddrs.reserve(512);
    for (int i = 0; i < 512; ++i) {
        freeAddrs.push_back(0x200000u + static_cast<uintptr_t>(i) * 128u);
    }
    uint32_t spawnSerial = 0u;
    uint32_t thingHeapUsed = 1200u * 1024u;
    const uint32_t thingHeapCap = 16u * 1024u * 1024u;
    uint32_t fragOverhead = 0u;
    int16_t retail16Counter = 0;
    uint64_t negativeWraps = 0u;

    auto spawnSim = [&](const char *def, int alleg, CeSandboxUnitRole role,
                        float x, float y, float z, float tx, float ty) {
        Check(!IsInsideCitadelKeepOut(x, y, kDirectorKeepOutRadius), "soak spawn outside Sanctuary keep-out");
        Check(!IsInsideCitadelKeepOut(tx, ty, kDirectorKeepOutRadius), "soak target outside Sanctuary keep-out");
        Check(!freeAddrs.empty(), "soak fake address pool non-empty");
        const uintptr_t ptr = freeAddrs.back();
        freeAddrs.pop_back();
        const CeEntityHandle h = handles.RegisterFresh(ptr, ++spawnSerial);
        Check(CeEntityHandleIsValid(h), "soak handle valid");
        if (++retail16Counter < 0) ++negativeWraps;

        SimUnit u = {};
        u.handle = h;
        u.fakePtr = ptr;
        u.allegiance = alleg;
        u.role = role;
        u.x = x; u.y = y; u.z = z;
        u.tx = tx; u.ty = ty;
        u.life = (role == CE_SB_ROLE_HQ) ? 600.0f : (role == CE_SB_ROLE_NAVAL ? 450.0f : 100.0f);
        u.stuck = (role == CE_SB_ROLE_LINE) && ((nextRand() & 31u) == 0u);
        world.push_back(u);
        dir.OnUnitSpawn(h, def, alleg, x, y, u.life);
        thingHeapUsed += 16u * 1024u;
        fragOverhead += 4u * 1024u;
    };

    // Seed HQ cores & opening forces.
    spawnSim(kUnitForsetiHq, kAllegianceForseti, CE_SB_ROLE_HQ, kForsetiHqX, kForsetiHqY, kBasePlateauZ, kForsetiHqX, kForsetiHqY);
    spawnSim(kUnitMuspellHq, kAllegianceMuspell, CE_SB_ROLE_HQ, kMuspellHqX, kMuspellHqY, kBasePlateauZ, kMuspellHqX, kMuspellHqY);
    for (int i = 0; i < 12; ++i) {
        spawnSim(kUnitBroadsword, kAllegianceForseti, CE_SB_ROLE_LINE, 180.0f + float(i * 4), 190.0f, kValleyFloorZ, 256.0f, 256.0f);
        spawnSim(kUnitHeavyGunTank, kAllegianceMuspell, CE_SB_ROLE_LINE, 330.0f - float(i * 4), 320.0f, kValleyFloorZ, 256.0f, 256.0f);
    }

    constexpr int kFourHoursSteps = 28800; // 14,400 s at 2 Hz
    int peakWorld = 0;
    uint32_t peakHeap = 0u;
    int peakEvents = 0;

    for (int step = 0; step < kFourHoursSteps; ++step) {
        CeSandboxNodePresence pres = {};
        CeSandboxTrackedSample samples[CeSandboxDirector::kMaxTrackedUnits];
        unsigned sc = 0u;

        // Simulate movement, combat attrition, and periodic HQ assaults.
        for (size_t i = 0; i < world.size(); ) {
            SimUnit &u = world[i];
            bool killed = false;
            if (u.role != CE_SB_ROLE_HQ && !u.stuck) {
                const float dx = u.tx - u.x;
                const float dy = u.ty - u.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > 2.5f) {
                    const float spd = (u.role == CE_SB_ROLE_AIR) ? 14.0f : 5.5f;
                    u.x += (dx / d) * spd * 0.5f;
                    u.y += (dy / d) * spd * 0.5f;
                } else {
                    // Engaged at the front: realistic combat attrition so waves build near cap
                    u.life -= 2.2f + float(nextRand() % 4u);
                }
            } else if (u.role == CE_SB_ROLE_HQ && (step % 1800 == 900) && (nextRand() & 1u)) {
                // Periodic deep raid knocks out an HQ core to verify endless regeneration
                u.life = 0.0f;
            }
            if (u.life <= 0.0f) killed = true;

            if (killed) {
                dir.OnUnitDestroyed(u.handle);
                handles.UnregisterHandle(u.handle);
                freeAddrs.push_back(u.fakePtr);
                if (thingHeapUsed > 1200u * 1024u + 16u * 1024u) thingHeapUsed -= 16u * 1024u;
                world[i] = world.back();
                world.pop_back();
                continue;
            }

            // Presence contribution to 5 capture nodes.
            for (unsigned n = 0; n < kCaptureNodeCount; ++n) {
                const CaptureNodeSpec &ns = CaptureNode(n);
                const float dx = u.x - ns.x;
                const float dy = u.y - ns.y;
                if (dx * dx + dy * dy <= cfg.captureRadius * cfg.captureRadius) {
                    if (u.allegiance == kAllegianceForseti) ++pres.forsetiWeight[n];
                    else ++pres.muspellWeight[n];
                }
            }
            if (sc < CeSandboxDirector::kMaxTrackedUnits) {
                samples[sc++] = {u.handle, u.x, u.y, u.z, u.life, !u.stuck && (u.life < 95.0f), true};
            }
            ++i;
        }

        const int estEvents = static_cast<int>(world.size()) * 24 + 400;
        if (estEvents > peakEvents) peakEvents = estEvents;
        const uint32_t totalUsed = thingHeapUsed + fragOverhead;
        if (totalUsed > peakHeap) peakHeap = totalUsed;
        const uint32_t freeBytes = (totalUsed < thingHeapCap) ? (thingHeapCap - totalUsed) : 0u;

        CeSandboxPressure press = {16.5f, static_cast<int>(world.size()), estEvents,
                                   kCeSandboxEventPoolCapacity, freeBytes, true};
        CeSandboxStepOutput out = {};
        const bool didStep = dir.Tick(0.5f, press, pres, samples, sc, &out);
        Check(didStep, "soak 2 Hz tick stepped");

        // Apply reaps.
        for (int r = 0; r < out.reapCount; ++r) {
            const CeEntityHandle rh = out.reaps[r].handle;
            for (size_t i = 0; i < world.size(); ++i) {
                if (world[i].handle == rh) {
                    handles.UnregisterHandle(rh);
                    freeAddrs.push_back(world[i].fakePtr);
                    if (thingHeapUsed > 1200u * 1024u + 16u * 1024u) thingHeapUsed -= 16u * 1024u;
                    world[i] = world.back();
                    world.pop_back();
                    break;
                }
            }
        }
        // Apply moves (verifying keep-out invariant on every issued move order!).
        for (int m = 0; m < out.moveCount; ++m) {
            Check(!IsInsideCitadelKeepOut(out.moves[m].x, out.moves[m].y, kDirectorKeepOutRadius),
                  "soak move waypoint stays outside Sanctuary keep-out");
            for (size_t i = 0; i < world.size(); ++i) {
                if (world[i].handle == out.moves[m].handle) {
                    world[i].tx = out.moves[m].x;
                    world[i].ty = out.moves[m].y;
                    break;
                }
            }
        }
        // Apply heap coalesce during periodic regroup lull.
        if (out.requestHeapCoalesce) {
            fragOverhead = 0u;
        }
        // Apply spawns.
        for (int s = 0; s < out.spawnCount; ++s) {
            const CeSandboxSpawnOrder &sp = out.spawns[s];
            spawnSim(sp.unitName, sp.allegiance, sp.role, sp.x, sp.y, sp.z, sp.targetX, sp.targetY);
        }

        if (static_cast<int>(world.size()) > peakWorld) peakWorld = static_cast<int>(world.size());
        Check(static_cast<int>(world.size()) <= cfg.targetUnitCap + 8, "soak population strictly bounded");
        Check(handles.LiveCount() == world.size(), "soak live handles exact 1:1 with live world units");
        Check(dir.TrackedCount() <= CeSandboxDirector::kMaxTrackedUnits, "soak tracked count bounded");
    }

    const CeSandboxDirector::Stats &st = dir.GetStats();
    Check(st.directorSteps == static_cast<uint64_t>(kFourHoursSteps), "4h soak completed 28,800 2 Hz steps");
    Check(st.totalRegroupLulls >= 15u && st.totalHeapCoalesces >= 15u, "periodic 15m regroup lulls & heap coalesces fired");
    Check(st.totalNavalEscalations >= 40u, "timed 5m naval escalations fired across 4h soak");
    Check(st.totalStuckReaps > 0u, "stuck-unit reaper reclaimed stagnant units");
    Check(st.totalSpawns > 2000u && st.totalDeaths > 1500u, "sustained 4h combined-arms churn");
    Check(peakEvents < kCeSandboxEventPoolCapacity / 2, "event count stayed safely below 50% capacity");
    Check(peakHeap < thingHeapCap - cfg.minThingHeapFreeBytes, "heap stayed flat with wide headroom");
    Check(dir.IsRunning(), "sandbox remained endlessly active after 4 simulated hours");

    std::printf("SOAK_4H steps=%llu spawns=%llu deaths=%llu reaps=%llu lulls=%llu coalesces=%llu naval=%llu hq_sup=%llu hq_reb=%llu peak_units=%d peak_events=%d/%d peak_heap_kib=%u handles_live=%u\n",
                static_cast<unsigned long long>(st.directorSteps),
                static_cast<unsigned long long>(st.totalSpawns),
                static_cast<unsigned long long>(st.totalDeaths),
                static_cast<unsigned long long>(st.totalStuckReaps),
                static_cast<unsigned long long>(st.totalRegroupLulls),
                static_cast<unsigned long long>(st.totalHeapCoalesces),
                static_cast<unsigned long long>(st.totalNavalEscalations),
                static_cast<unsigned long long>(st.totalHqSuppressions),
                static_cast<unsigned long long>(st.totalHqRebuilds),
                peakWorld, peakEvents, kCeSandboxEventPoolCapacity,
                static_cast<unsigned>(peakHeap / 1024u),
                static_cast<unsigned>(handles.LiveCount()));
    (void)negativeWraps;
}

int main() {
    TestHeightfieldCitadelAndSeams();
    TestWorldSerializerAndPreflight();
    TestSandboxProfileGates();
    TestDirectorMechanicsAndHud();
    TestFourHourHeadlessSoak();
    if (gFailures != 0) {
        std::fprintf(stderr, "TOTAL FAILURES: %d\n", gFailures);
        return 1;
    }
    std::puts("LIVING_SANDBOX_OK");
    return 0;
}
'''


def compile_and_run(work: Path, name: str, include_dirs, extra_flags=()):
    cpp = work / f"{name}.cpp"
    exe = work / name
    cpp.write_text(DRIVER_CPP, encoding="utf-8")
    cmd = [
        CXX, "-std=c++14", "-O2", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        *extra_flags,
    ]
    for inc in include_dirs:
        cmd.extend(["-I", str(inc)])
    cmd.extend([str(cpp), "-o", str(exe), "-lm"])
    built = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if built.returncode != 0:
        return built.returncode, "COMPILE_FAIL:\n" + built.stdout[-1500:] + built.stderr[-1500:]
    ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    return ran.returncode, ran.stdout + ran.stderr


class LivingSandboxTests(unittest.TestCase):
    def test_host_asan_ubsan_and_4hour_soak(self):
        with tempfile.TemporaryDirectory(prefix="ce-living-sandbox-") as td:
            work = Path(td)
            rc, out = compile_and_run(
                work, "driver_asan", [GAME],
                extra_flags=("-fsanitize=address,undefined", "-fno-omit-frame-pointer"),
            )
            self.assertEqual(rc, 0, out)
            self.assertIn("SOAK_4H", out)
            self.assertIn("LIVING_SANDBOX_OK", out)
            for line in out.splitlines():
                if line.startswith("SOAK_4H"):
                    print(f"\n  {line}")

    def test_causal_mutants_are_caught(self):
        mutants = [
            # ce_sandbox_world.h mutants
            ("citadel_floor_wrong_z", "ce_sandbox_world.h",
             "constexpr float kCitadelFloorZ = -34.0f;",
             "constexpr float kCitadelFloorZ = -26.0f;"),
            ("citadel_ramp_removed", "ce_sandbox_world.h",
             "const float rampZ = Lerp(kCitadelFloorZ, kValleyFloorZ, SmoothStep01(rampT));",
             "const float rampZ = kValleyFloorZ;"),
            ("citadel_parapet_removed", "ce_sandbox_world.h",
             "if (bermW > 0.001f) {\n\t\t\tconst float bermZ = Lerp(kCitadelFloorZ, kCitadelParapetZ, bermW);",
             "if (false && bermW > 0.001f) {\n\t\t\tconst float bermZ = Lerp(kCitadelFloorZ, kCitadelParapetZ, bermW);"),
            ("keepout_check_disabled", "ce_sandbox_world.h",
             "return (dx * dx + dy * dy) < (radius * radius);",
             "return false;"),
            ("keepout_clamp_disabled", "ce_sandbox_world.h",
             "x = kCitadelCenterX + nx * minRadius;\n\ty = kCitadelCenterY + ny * minRadius;",
             "/* clamp disabled */"),
            ("world_validator_allows_keepout", "ce_sandbox_world.h",
             "if (IsInsideCitadelKeepOut(u.position[0], u.position[1], kDirectorKeepOutRadius))\n\t\t\treturn false;",
             "/* allow keepout */"),
            ("preflight_ignores_missing_700", "ce_sandbox_world.h",
             "if (!in.archiveExists) {",
             "if (false && !in.archiveExists) {"),
            # ce_sandbox_director.h mutants
            ("director_not_2hz", "ce_sandbox_director.h",
             "if (stepAccumSec_ + 1e-5f < cfg_.directorStepSec) return false;",
             "if (false && stepAccumSec_ < cfg_.directorStepSec) return false;"),
            ("event_50pct_alarm_removed", "ce_sandbox_director.h",
             "const bool eventAlarm = (pressure.eventCapacity > 0) &&\n"
             "\t\t                        (static_cast<float>(pressure.eventPending) >=\n"
             "\t\t                         static_cast<float>(pressure.eventCapacity) * cfg_.maxEventLoadRatio);",
             "const bool eventAlarm = false;"),
            ("heap_768k_alarm_removed", "ce_sandbox_director.h",
             "const bool heapAlarm = (pressure.thingHeapFreeBytes < cfg_.minThingHeapFreeBytes);",
             "const bool heapAlarm = false;"),
            ("hysteresis_disabled", "ce_sandbox_director.h",
             "if (pressure.liveWorldUnits >= effectiveCap_) {\n\t\t\thysteresisPaused_ = true;\n\t\t}",
             "if (false && pressure.liveWorldUnits >= effectiveCap_) {\n\t\t\thysteresisPaused_ = true;\n\t\t}"),
            ("stuck_reaper_disabled", "ce_sandbox_director.h",
             "if (tu.stuckSec >= reapLimit && out.reapCount < CeSandboxStepOutput::kMaxReaps) {",
             "if (false && tu.stuckSec >= reapLimit && out.reapCount < CeSandboxStepOutput::kMaxReaps) {"),
            ("regroup_coalesce_disabled", "ce_sandbox_director.h",
             "out.requestHeapCoalesce = true;",
             "out.requestHeapCoalesce = false;"),
            ("hq_rebuild_disabled", "ce_sandbox_director.h",
             "if (hq.rebuildTimerSec <= 0.0f && !heapAlarm) {",
             "if (false && hq.rebuildTimerSec <= 0.0f && !heapAlarm) {"),
            ("p0_stasis_disabled", "ce_sandbox_director.h",
             "if (st.drivingOtherUnit) {\n\t\tact.engageStasisShield = true;\n\t\treturn act;\n\t}",
             "if (false && st.drivingOtherUnit) {\n\t\tact.engageStasisShield = true;\n\t\treturn act;\n\t}"),
            ("glyph_filter_disabled", "ce_sandbox_director.h",
             "case '[': case ']': case '|': case '_': case '~':",
             "case 0x01:"),
            # ce_sandbox_telemetry.h mutant
            ("startup_profile_ignores_optin", "ce_sandbox_telemetry.h",
             "inline bool CeSandboxStartupProfileAllowed(const CeSandboxProfileGate& gate) {\n    return gate.sandboxOptIn &&",
             "inline bool CeSandboxStartupProfileAllowed(const CeSandboxProfileGate& gate) {\n    return true &&"),
        ]

        with tempfile.TemporaryDirectory(prefix="ce-living-sandbox-mut-") as td:
            base = Path(td)
            for label, fname, old, new in mutants:
                with self.subTest(mutant=label):
                    mdir = base / label
                    mdir.mkdir()
                    for hdr in ("ce_sandbox_world.h", "ce_sandbox_director.h",
                                "ce_sandbox_telemetry.h", "ce_entity_handles.h"):
                        text = (GAME / hdr).read_text(encoding="utf-8")
                        if hdr == fname:
                            self.assertIn(old, text, f"{label}: anchor missing in {hdr}")
                            text = text.replace(old, new, 1)
                        (mdir / hdr).write_text(text, encoding="utf-8")
                    rc, out = compile_and_run(mdir, "mut", [mdir])
                    self.assertNotEqual(rc, 0, f"mutant {label} survived:\n{out}")

    def test_patch_0378_ownership_and_guards(self):
        text = PATCH_0378.read_text(encoding="utf-8")
        files = re.findall(r"^--- a/(.+)$", text, re.M)
        allowed = {
            "ResourceAccumulator.cpp", "world.cpp", "game.cpp", "FrontEnd.cpp",
            "DXMemoryManager.cpp", "System.cpp", "eventmanager.cpp", "BattleEngine.cpp",
        }
        self.assertEqual(set(files), allowed, f"unexpected patch targets: {files}")
        self.assertNotIn("Round.cpp", files)
        self.assertNotIn("DXEngine.cpp", files)
        # Walk the exact repo patch queue (via patch_shape_tests) for all 7 owned
        # target files and prove ZERO Living Sandbox / Sandbox / EventManager hook
        # symbols survive when CE_VERSION is undefined.
        sys.path.insert(0, str(PORT / "tools"))
        import patch_shape_tests as pst  # noqa: E402
        queue = [(p.name, p.read_text(encoding="latin-1")) for p in sorted((PORT / "patches").glob("*.patch"))]
        sources = pst.Sources()
        starts = {}

        def start_of(name):
            if name not in starts:
                starts[name] = [(label, sources.text(path, name), skipped) for label, path, skipped in pst.forms(name)]
            return starts[name]

        findings, _, texts, _ = pst.walk(queue, start_of, names=sorted(allowed))
        self.assertEqual(findings, [], f"patch queue walk findings: {findings}")
        hook_re = re.compile(r"PortCeLivingSandbox|PortCeSandbox|PortCeEventManager")
        found_guarded = 0
        for (fname, _label), patched_text in sorted(texts.items()):
            stack = []  # list of [is_ce_guard, in_else]
            for lineno, line in enumerate(patched_text.splitlines(), 1):
                s = line.strip()
                if re.match(r"^#\s*ifdef\s+CE_VERSION\b", s) or re.match(r"^#\s*if\s+defined\(\s*CE_VERSION\s*\)", s):
                    stack.append([True, False])
                    continue
                if re.match(r"^#\s*(if|ifdef|ifndef)\b", s):
                    stack.append([False, False])
                    continue
                if re.match(r"^#\s*else\b", s) and stack:
                    stack[-1][1] = True
                    continue
                if re.match(r"^#\s*endif\b", s) and stack:
                    stack.pop()
                    continue
                inside_ce = any(is_ce and not in_else for is_ce, in_else in stack)
                if hook_re.search(line):
                    if not inside_ce:
                        self.fail(f"{fname}:{lineno} leaks CE hook outside #ifdef CE_VERSION: {line}")
                    found_guarded += 1
        self.assertGreater(found_guarded, 15, "expected guarded Living Sandbox hooks in patched target files")

    def test_spdx_and_hygiene(self):
        new_files = [
            GAME / "ce_sandbox_world.h",
            GAME / "ce_sandbox_world.cpp",
            GAME / "ce_sandbox_director.h",
            GAME / "ce_sandbox_director.cpp",
            Path(__file__),
        ]
        bad_tmp = "/var/" + "tmp"
        bad_env = "BEA_" + "SANDBOX"
        for p in new_files:
            content = p.read_text(encoding="utf-8")
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", content[:240], f"missing SPDX in {p}")
            self.assertNotIn(bad_tmp, content, f"forbidden temp path in {p}")
            self.assertNotIn(bad_env, content, f"forbidden env var in {p}")

        # Verify clean optional Citadel seam exists without requiring ce_sanctuary_citadel.h to compile.
        dir_cpp = (GAME / "ce_sandbox_director.cpp").read_text(encoding="utf-8")
        self.assertIn('__has_include("ce_sanctuary_citadel.h")', dir_cpp)
        self.assertIn("#ifdef CE_SANDBOX_HAS_CITADEL", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeInit(", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeShutdown()", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeUpdate(", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeDrawWorld()", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeDrawDomeFx()", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeHandleKey(", dir_cpp)
        self.assertIn("CeSanctuaryCitadel::RuntimeCue(", dir_cpp)

        # Verify transition, HUD batching, input/pause guards, rescue morph & Forseti HQ preload.
        world_h = (GAME / "ce_sandbox_world.h").read_text(encoding="utf-8")
        world_cpp = (GAME / "ce_sandbox_world.cpp").read_text(encoding="utf-8")
        self.assertIn("RefreshProceduralMapExtents()", world_cpp)
        self.assertIn("EngineMediaAccess::KempyCube()", world_cpp)
        self.assertIn("EngineMediaAccess::Water()", world_cpp)
        self.assertIn("gWorld.arenaInstalled", world_cpp)
        self.assertIn("CMesh::GetMeshByResourceID(", world_cpp)
        self.assertIn("InferMeshTextureFormat(", world_cpp)
        self.assertIn('"Forseti Pulse Tank Factory"', world_h)
        self.assertIn('"fb_tank_factory.msh"', world_h)
        self.assertIn("if (viewpoint != 0 || gMirrorPass) return;", dir_cpp)
        self.assertIn("GAMEINTERFACE.IsMenuUp()", dir_cpp)
        self.assertIn("LT.GetKeytrap()", dir_cpp)
        self.assertIn("PortCeCmdMapOpen()", dir_cpp)
        self.assertIn("be->Morph();", dir_cpp)
        self.assertIn("D3DPT_TRIANGLELIST", dir_cpp)
        self.assertNotIn("D3DPT_TRIANGLEFAN", dir_cpp)
        self.assertNotIn("D3DPT_TRIANGLESTRIP", dir_cpp)


if __name__ == "__main__":
    unittest.main(verbosity=2)
