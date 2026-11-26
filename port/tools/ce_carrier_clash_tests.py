#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clean-room Carrier Clash (F.S. Venturer vs. Fenrir) host verification suite.

Compiles a C++14 host driver with ASan/UBSan against:
  - port/game/ce_carrier_clash_world.h
  - port/game/ce_carrier_clash.h
  - port/game/ce_entity_handles.h / ce_entity_handles.cpp

Verifies:
  1. Procedural 512x512m basalt archipelago heightfield (+/- 55 m clamp, heights
     inside [-54 m, +14 m], open deep strait at every sample, flat landing pad,
     clear horizon behind F.S. Venturer from the player's start, three height
     classes (reef bar, crescent island and cove, sea-stack), exact 64x64 patch
     seam continuity, sunset lightmap range with cast shadows, and the MIXERMAP
     alphas' [-62, +62] and rock >= max(0, sand) - 62 proof at every sample).
  2. Clean-room version-50 base & real world serializer + round-trip parser
     (zero mission scripts, zero named meshes, explicit Fenrir Multiplayer
     rejection in every layer, 325-byte PMSH/PMS2 mesh stub builder).
  3. Strict preflight asset evaluator for every missing/corrupt retail asset.
  4. CeCarrierClashDirector state machine (32-bit CeEntityHandle flagship
     tracking past 65,536 generations, Victory/Defeat transitions and
     irreversibility, capped escort wave pacing, corridor patrol bounds,
     >= 140 m broadside lanes with >= 18 m altitude stagger, the 125 m live
     separation guard, hull-keyed battle acts and the ~4 s outcome hold).
  5. 9-slice HUD status strip layout geometry across aspect ratios & FONT_SMALL
     glyph safety (no [, ], |, _, etc.).
  6. Long-session soak (>65 minutes simulated time, >45,000 spawn/destroy cycles
     with live CeEntityHandleTable churn).
  7. 34 causal mutants across ce_carrier_clash_world.h and ce_carrier_clash.h
     (heightfield, landing pad, sightline fan, cove, cast shadows, sun bearing,
     mixer alpha proof, serializer, preflight, caps, glyphs, broadside lanes,
     live separation guard, three-act arc and the held level result).
  8. Patch 0377 CE_VERSION guard hygiene, forbidden-asset source hygiene, and
     the CE-original opening title (FONT_SMALL-safe, fits its banner).
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

    // Directional sun lightmap (the game's own sunset vector, with and without
    // cast shadows) stays inside MIXERMAP [14..46] dusk byte band.
    uint8_t minLight = 255, maxLight = 0;
    for (int y = 0; y < kCells; y += 4) {
        for (int x = 0; x < kCells; x += 4) {
            const uint8_t l = ComputeCellLight(x, y, kSunDirX, kSunDirY, kSunDirZ);
            const uint8_t ls = ComputeCellLightShadowed(x, y, kSunDirX, kSunDirY, kSunDirZ);
            Check(l >= 14u && l <= 46u, "cell light within [14..46] dusk ceiling");
            Check(ls >= 14u && ls <= l, "shadowed cell light within [14..unshadowed]");
            if (l < minLight) minLight = l;
            if (l > maxLight) maxLight = l;
        }
    }
    Check(maxLight > minLight + 8u, "sea-stacks produce directional sun shading contrast");
}

// Elevation above sea level (metres, positive up) at every integer sample.
static float gElev[513 * 513];

static float ElevAt(int x, int y) { return gElev[y * 513 + x]; }

static float BoxPeak(int x0, int y0, int x1, int y1) {
    float top = -1000.0f;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (ElevAt(x, y) > top) top = ElevAt(x, y);
    return top;
}

// The sculpted archipelago: an open strait, a flat landing pad, a clear horizon
// behind F.S. Venturer, three height classes, a sheltered cove, sunset lighting
// with cast shadows, and the mixer alphas' no-wrap proof at every sample.
static void TestArchipelagoSculpting() {
    using namespace CeCarrierClashWorld;
    float minZ = 1000.0f, maxZ = -1000.0f;
    for (int y = 0; y <= kCells; ++y) {
        for (int x = 0; x <= kCells; ++x) {
            const float z = HeightAt(static_cast<float>(x), static_cast<float>(y));
            gElev[y * 513 + x] = kWaterLevel - z;
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
        }
    }
    Check(minZ >= -54.0f && maxZ <= 14.0f, "every height inside [-54 m, +14 m]");

    // 1. The whole flagship strait is open deep water, at every sample.
    bool straitDeep = true;
    for (int y = 185; y <= 345; ++y)
        for (int x = 170; x <= 345; ++x)
            if (!(ElevAt(x, y) < -10.0f)) straitDeep = false;
    Check(straitDeep, "strait x 170..345, y 185..345 is deep water at every sample");
    Check(kStraitMinX <= 168.0f && kStraitMaxX >= 352.0f && kStraitMinY <= 185.0f && kStraitMaxY >= 345.0f,
          "the deep-water keep-out covers both patrol lanes");

    // 2. The landing pad is exactly flat out to 9.5 m, and the player starts on it.
    Check(kBastionPadRadius >= 9.5f, "landing pad radius is 9.5 m or more");
    bool padFlat = true;
    for (float dy = -9.5f; dy <= 9.5f; dy += 0.5f)
        for (float dx = -9.5f; dx <= 9.5f; dx += 0.5f)
            if (dx * dx + dy * dy <= 9.5f * 9.5f &&
                std::fabs(HeightAt(kBastionX + dx, kBastionY + dy) - kBastionTopZ) > 0.01f) padFlat = false;
    Check(padFlat, "landing pad is flat at the bastion height out to 9.5 m");
    {
        UnitSpawnSpec oob[kMaxActors] = {};
        const WorldPlan plan = BuildDefaultWorldPlan(oob, BuildDefaultOrderOfBattle(oob, kMaxActors, false));
        const float sx = plan.startPosition[0] - kBastionX, sy = plan.startPosition[1] - kBastionY;
        Check(sx * sx + sy * sy <= 9.5f * 9.5f &&
              std::fabs(HeightAt(plan.startPosition[0], plan.startPosition[1]) - plan.startPosition[2]) < 0.01f,
              "the player starts standing on the landing pad");
        Check(plan.startPosition[0] == kLaunchEyeX && plan.startPosition[1] == kLaunchEyeY,
              "the sightline fan is measured from the player's start");
    }
    // Nothing in the view north of the pad stands above it nearby.
    bool lipLow = true;
    for (int y = 145; y <= 185; ++y)
        for (int x = 228; x <= 288; ++x)
            if (ElevAt(x, y) > kBastionElev + 0.01f) lipLow = false;
    Check(lipLow, "the headland's lip and overlook never rise above the pad");

    // 3. F.S. Venturer's silhouette: from the player's start, everything behind
    //    any Venturer station (bearings 67 to 5 degrees west of north, out to the
    //    map edge) stays below the pad, so no land breaks the horizon behind her.
    bool fanClear = true;
    for (int y = 0; y <= kCells; ++y) {
        for (int x = 0; x <= kCells; ++x) {
            const float dx = static_cast<float>(x) - kLaunchEyeX;
            const float dy = static_cast<float>(y) - kLaunchEyeY;
            if (dy <= 0.0f || dx * dx + dy * dy < 70.0f * 70.0f) continue;
            const float bearing = std::atan2(dx, dy) * 57.29578f;
            if (bearing < -67.0f || bearing > -5.0f) continue;
            if (!(ElevAt(x, y) < kBastionElev - 0.25f)) fanClear = false;
        }
    }
    Check(fanClear, "no land behind F.S. Venturer rises to the pad's height");
    for (int i = 0; i < 4; ++i) {
        const float *v = CeCarrierClashVenturerWaypoint(i);
        const float bearing = std::atan2(v[0] - kLaunchEyeX, v[1] - kLaunchEyeY) * 57.29578f;
        Check(bearing > -45.0f && bearing < -15.0f, "every Venturer station sits well inside the clear fan");
    }

    // 4. Three height classes: low reef, crescent island, tall sea-stack.
    const float reefPeak = BoxPeak(25, 315, 215, 480);
    const float crescentPeak = BoxPeak(370, 225, 505, 360);
    const float stackPeak = BoxPeak(245, 405, 320, 480);
    Check(reefPeak >= 4.5f && reefPeak <= kReefMaxElev + 0.01f, "reef bar is low (4.5 to 6.5 m)");
    Check(crescentPeak >= 10.0f && crescentPeak <= 27.0f, "crescent island is mid height (10 to 27 m)");
    Check(stackPeak >= 28.0f && stackPeak <= kMaxLandElev + 0.01f, "sea-stack landmark is tall (28 to 43 m)");
    Check(kReefMaxElev < kBastionElev, "reef bar tops out below the landing pad");

    // 5. The crescent shelters a shallow sandy cove.
    int coveCells = 0, coveSand = 0;
    for (int y = static_cast<int>(kCrescentY) - 10; y <= static_cast<int>(kCrescentY) + 10; ++y) {
        for (int x = static_cast<int>(kCrescentX) - 10; x <= static_cast<int>(kCrescentX) + 10; ++x) {
            const float cdx = static_cast<float>(x) - kCrescentX, cdy = static_cast<float>(y) - kCrescentY;
            if (cdx * cdx + cdy * cdy > 10.0f * 10.0f) continue;
            const float e = ElevAt(x, y);
            if (!(e > -4.5f && e < -1.0f)) continue;
            ++coveCells;
            if (ComputeMixAlphas(static_cast<float>(x), static_cast<float>(y)).sand >= 50) ++coveSand;
        }
    }
    Check(coveCells >= 100, "the crescent holds a shallow cove");
    Check(coveSand >= 60, "the cove floor is sand");

    // 6. Sunset lighting. The sun stands low in the north-north-west, behind
    //    F.S. Venturer as seen from the pad; faces turned to it are bright,
    //    faces turned away are dark, the stack throws a cast shadow down its
    //    lee, and its sun side is a sheer face where its lee is a long tail.
    const float sunHoriz = std::sqrt(kSunDirX * kSunDirX + kSunDirY * kSunDirY);
    const float sunLen = std::sqrt(sunHoriz * sunHoriz + kSunDirZ * kSunDirZ);
    Check(sunLen > 0.99f && sunLen < 1.01f, "sun vector is unit length");
    Check(kSunDirZ / sunLen < -0.10f && kSunDirZ / sunLen > -0.30f, "sun stands 6 to 17 degrees above the horizon");
    Check((kSunDirX * kSunAzX + kSunDirY * kSunAzY) / sunHoriz > 0.999f, "sun bearing matches the landforms' sun side");
    const float sunBearing = std::atan2(kSunAzX, kSunAzY) * 57.29578f;
    const float venturerBearing = std::atan2(kVenturerStartX - kLaunchEyeX, kVenturerStartY - kLaunchEyeY) * 57.29578f;
    Check(std::fabs(sunBearing - venturerBearing) < 25.0f, "the sunset glow stands behind F.S. Venturer from the pad");
    int sunFaces = 0, leeFaces = 0, shadowed = 0, peakX = 246, peakY = 406;
    float sunLight = 0.0f, leeLight = 0.0f;
    for (int y = 406; y <= 479; ++y) {
        for (int x = 246; x <= 319; ++x) {
            if (ElevAt(x, y) > ElevAt(peakX, peakY)) { peakX = x; peakY = y; }
            if (!(ElevAt(x, y) > 1.0f)) continue;
            const uint8_t flat = ComputeCellLight(x, y, kSunDirX, kSunDirY, kSunDirZ);
            const uint8_t cast = ComputeCellLightShadowed(x, y, kSunDirX, kSunDirY, kSunDirZ);
            if (cast + 5 <= flat) ++shadowed;
            // A face looks downhill: the direction elevation falls away in.
            const float gx = -0.5f * (ElevAt(x + 1, y) - ElevAt(x - 1, y));
            const float gy = -0.5f * (ElevAt(x, y + 1) - ElevAt(x, y - 1));
            const float slope = std::sqrt(gx * gx + gy * gy);
            if (!(slope > 0.5f)) continue;
            const float facing = (gx * kSunAzX + gy * kSunAzY) / slope;
            if (facing > 0.5f) { ++sunFaces; sunLight += static_cast<float>(flat); }
            if (facing < -0.5f) { ++leeFaces; leeLight += static_cast<float>(flat); }
        }
    }
    Check(sunFaces >= 20 && leeFaces >= 20, "the stack has both sun-facing and lee-facing slopes");
    if (sunFaces > 0 && leeFaces > 0) {
        Check(sunLight / sunFaces >= 28.0f, "sun-facing cliffs catch the sunset light");
        Check(leeLight / leeFaces <= 21.0f, "lee-facing slopes fall dark");
    }
    Check(shadowed >= 10, "the stack casts a shadow down its lee");
    // Asymmetric profile: from the summit the sea is close on the sun side (a
    // sheer face) and far down the lee (a long tail).
    float sunReach = 0.0f, leeReach = 0.0f;
    for (float t = 0.5f; t < 90.0f; t += 0.5f) {
        if (sunReach == 0.0f && kWaterLevel - HeightAt(static_cast<float>(peakX) + kSunAzX * t,
                                                       static_cast<float>(peakY) + kSunAzY * t) < 0.0f) sunReach = t;
        if (leeReach == 0.0f && kWaterLevel - HeightAt(static_cast<float>(peakX) - kSunAzX * t,
                                                       static_cast<float>(peakY) - kSunAzY * t) < 0.0f) leeReach = t;
    }
    Check(sunReach > 0.0f && sunReach <= 22.0f, "the stack's sun side is a sheer face into the sea");
    Check(leeReach >= 1.25f * sunReach && leeReach >= 18.0f, "the stack's lee is a long tail");

    // 7. MIXERMAP alphas at every sample: both inside [-62, +62] and rock >=
    //    max(0, sand) - 62 (the convex set that keeps CHeightField's signed
    //    32-bit blend from wrapping); sheltered flat soil is clean grass, cliffs
    //    and the launch headland are basalt rock, bare rocky landforms omit pale
    //    sand rings, sheltered shorelines carry sand, and open water keeps none.
    bool alphaRange = true, alphaConvex = true, flatsGrass = true, cliffsRock = true, bareNoSand = true;
    int grassFlats = 0, rockCliffs = 0, sandSkirt = 0, bareShores = 0;
    for (int y = 0; y <= kCells; ++y) {
        for (int x = 0; x <= kCells; ++x) {
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            const MixAlphas a = ComputeMixAlphas(fx, fy);
            if (a.sand < -62 || a.sand > 62 || a.rock < -62 || a.rock > 62) alphaRange = false;
            if (a.rock < (a.sand > 0 ? a.sand : 0) - 62) alphaConvex = false;
            const TerrainSample ts = SampleTerrain(fx, fy);
            const float e = ElevAt(x, y);
            if (e > -1.0f && e < 1.0f) {
                if (ts.bare >= 0.99f) {
                    ++bareShores;
                    if (a.sand != -62) bareNoSand = false;
                } else if (a.sand >= 50) {
                    ++sandSkirt;
                }
            }
            if (!(e > 3.0f)) continue;
            const float slope = SlopeAt(fx, fy);
            if (slope >= 1.2f) {
                ++rockCliffs;
                if (a.rock < 50) cliffsRock = false;
            } else if (slope <= 0.5f && e < 27.0f && ts.bare == 0.0f) {
                ++grassFlats;
                if (a.sand != -62 || a.rock != -62) flatsGrass = false;
            }
        }
    }
    Check(alphaRange, "every mixer alpha inside [-62, +62]");
    Check(alphaConvex, "every sample keeps rock >= max(0, sand) - 62");
    Check(flatsGrass && grassFlats >= 80, "sheltered flat terraces above the shore are clean grass");
    Check(cliffsRock && rockCliffs >= 50, "steep faces are clean basalt rock");
    Check(sandSkirt >= 100, "sheltered shorelines carry a sand skirt");
    Check(bareNoSand && bareShores >= 40, "bare rocky landforms omit pale waterline sand rings");
    const MixAlphas pad = ComputeMixAlphas(kBastionX, kBastionY);
    const MixAlphas sea = ComputeMixAlphas(kVenturerStartX, kVenturerStartY);
    Check(pad.sand == -62 && pad.rock == 62, "the launch headland pad is basalt rock");
    Check(sea.sand == -62 && sea.rock == -62, "open water keeps no mixer layer");
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

    // Truncated world buffer (header or 78-byte tail) and trailing garbage fail verification cleanly.
    Check(!VerifyWorldBody(realBuf, 32u, &realSum), "truncated world rejected");
    Check(!VerifyWorldBody(realBuf, realLen - 6u, &realSum), "truncated 78-byte world tail rejected");
    if (realLen + 1u <= sizeof(realBuf)) {
        realBuf[realLen] = 0xAAu;
        Check(!VerifyWorldBody(realBuf, realLen + 1u, &realSum), "trailing bytes past world tail rejected");
    }

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

static float PlanarDist(float ax, float ay, float bx, float by) {
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// The two ~100 m flagships keep their own sides of the strait: every pair of
// authored patrol legs, the opening stations, and any live approach.
static void TestBroadsideLanesAndSeparationGuard() {
    using namespace CeCarrierClashWorld;

    // 1. Authored waypoints: each ship inside its own lane, over deep water, with
    //    >= 140 m between ANY Venturer leg and ANY Fenrir leg (so the ships are
    //    clear of each other even when they are on different legs), and >= 18 m
    //    of altitude stagger.
    for (int i = 0; i < 4; ++i) {
        const float *v = CeCarrierClashVenturerWaypoint(i);
        Check(v[0] >= kCeCcWestLaneMinX && v[0] <= kCeCcWestLaneMaxX, "Venturer waypoint inside West lane");
        Check(v[1] >= kCeCcLaneMinY && v[1] <= kCeCcLaneMaxY, "Venturer waypoint inside lane Y band");
        Check(HeightAt(v[0], v[1]) > kWaterLevel + 10.0f, "every Venturer waypoint over deep water");
        for (int j = 0; j < 4; ++j) {
            const float *f = CeCarrierClashFenrirWaypoint(j);
            Check(f[0] >= kCeCcEastLaneMinX && f[0] <= kCeCcEastLaneMaxX, "Fenrir waypoint inside East lane");
            Check(f[1] >= kCeCcLaneMinY && f[1] <= kCeCcLaneMaxY, "Fenrir waypoint inside lane Y band");
            Check(HeightAt(f[0], f[1]) > kWaterLevel + 10.0f, "every Fenrir waypoint over deep water");
            Check(f[0] - v[0] >= kCeCcMinLaneSeparation, "patrol lanes stay >= 140 m apart on X");
            Check(PlanarDist(v[0], v[1], f[0], f[1]) >= kCeCcMinLaneSeparation,
                  "any Venturer leg vs any Fenrir leg >= 140 m");
            Check(std::fabs(v[2] - f[2]) >= kCeCcMinAltitudeStagger, "flagship altitude stagger >= 18 m");
        }
    }
    Check(kCeCcSeparationGuardDist >= 125.0f && kCeCcSeparationGuardDist < kCeCcMinLaneSeparation,
          "guard distance is 125 m or more and inside the authored lane separation");
    Check(kCeCcHardSeparationDist < kCeCcSeparationGuardDist && kCeCcHardSeparationDist >= 100.0f,
          "hard separation sits inside the guard distance and outside a ~100 m hull");

    // 2. Opening stations in the authored world match the lanes.
    UnitSpawnSpec oob[kMaxActors] = {};
    const unsigned count = BuildDefaultOrderOfBattle(oob, kMaxActors, false);
    Check(count >= 2u, "order of battle has both flagships");
    Check(StrEqualsIgnoreCase(oob[0].unitDef, kVenturerUnit) && StrEqualsIgnoreCase(oob[1].unitDef, kFenrirUnit),
          "flagships lead the order of battle");
    Check(oob[0].position[0] >= kCeCcWestLaneMinX && oob[0].position[0] <= kCeCcWestLaneMaxX,
          "Venturer opens in the West lane");
    Check(oob[1].position[0] >= kCeCcEastLaneMinX && oob[1].position[0] <= kCeCcEastLaneMaxX,
          "Fenrir opens in the East lane");
    Check(PlanarDist(oob[0].position[0], oob[0].position[1], oob[1].position[0], oob[1].position[1]) >=
          kCeCcMinLaneSeparation, "flagships open >= 140 m apart");
    Check(std::fabs(oob[0].position[2] - oob[1].position[2]) >= kCeCcMinAltitudeStagger,
          "flagships open with >= 18 m altitude stagger");
    Check(oob[0].position[2] == kCeCcVenturerZ && oob[1].position[2] == kCeCcFenrirZ,
          "opening altitudes are the lane altitudes");
    for (unsigned i = 0; i < count; ++i) {
        Check(HeightAt(oob[i].position[0], oob[i].position[1]) > kWaterLevel + 10.0f,
              "every opening unit is over deep water");
    }

    // 3. Patrol orders over a long run stay in-lane and never closer than 140 m.
    {
        CeCarrierClashDirector dir;
        dir.Arm();
        dir.BeginLevel();
        dir.OnUnitSpawn(CeEntityHandlePack(1u, 1u), kVenturerUnit, kAllegianceForseti, 100.0f, 100.0f);
        dir.OnUnitSpawn(CeEntityHandlePack(2u, 1u), kFenrirUnit, kAllegianceMuspell, 100.0f, 100.0f);
        float lastV[2] = {kVenturerStartX, kVenturerStartY}, lastF[2] = {kFenrirStartX, kFenrirStartY};
        int legs = 0;
        for (int t = 0; t < 12000; ++t) {
            CeCarrierClashSpawnOrder spawns[4] = {};
            int n = 0;
            CeCarrierClashPatrolOrder vp = {}, fp = {};
            dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
            if (vp.valid) { lastV[0] = vp.x; lastV[1] = vp.y; ++legs; }
            if (fp.valid) { lastF[0] = fp.x; lastF[1] = fp.y; }
            Check(PlanarDist(lastV[0], lastV[1], lastF[0], lastF[1]) >= kCeCcMinLaneSeparation,
                  "live patrol targets never closer than 140 m");
        }
        Check(legs >= 40, "patrol keeps cycling legs");
        Check(dir.SeparationGuardCount() == 0, "guard stays quiet while no positions are reported");
    }

    // 4. Live guard: ships reported closer than 125 m are both sent to their own
    //    lanes at once (not at the next 14 s patrol step), on opposite sides, at
    //    staggered altitudes; then it stays quiet for its cooldown.
    {
        CeCarrierClashDirector dir;
        dir.Arm();
        dir.BeginLevel();
        dir.OnUnitSpawn(CeEntityHandlePack(1u, 1u), kVenturerUnit, kAllegianceForseti, 100.0f, 100.0f);
        dir.OnUnitSpawn(CeEntityHandlePack(2u, 1u), kFenrirUnit, kAllegianceMuspell, 100.0f, 100.0f);
        CeCarrierClashSpawnOrder spawns[4] = {};
        int n = 0;
        CeCarrierClashPatrolOrder vp = {}, fp = {};
        dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);     // the initial patrol step
        Check(vp.valid && fp.valid, "initial patrol emitted before guard test");

        dir.UpdateFlagshipPositions(180.0f, 262.0f, 340.0f, 266.0f);  // 160 m apart: quiet
        dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(!vp.valid && !fp.valid && dir.SeparationGuardCount() == 0, "guard quiet at 160 m");

        dir.UpdateFlagshipPositions(232.0f, 270.0f, 290.0f, 274.0f);  // 58 m apart: the reported clip
        dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(vp.valid && fp.valid && dir.SeparationGuardCount() == 1, "guard fires inside 125 m");
        Check(vp.x >= kCeCcWestLaneMinX && vp.x <= kCeCcWestLaneMaxX, "guard sends Venturer to the West lane");
        Check(fp.x >= kCeCcEastLaneMinX && fp.x <= kCeCcEastLaneMaxX, "guard sends Fenrir to the East lane");
        Check(PlanarDist(vp.x, vp.y, fp.x, fp.y) >= kCeCcMinLaneSeparation, "guard targets >= 140 m apart");
        Check(std::fabs(vp.z - fp.z) >= kCeCcMinAltitudeStagger, "guard targets keep the altitude stagger");
        Check(HeightAt(vp.x, vp.y) > kWaterLevel + 10.0f && HeightAt(fp.x, fp.y) > kWaterLevel + 10.0f,
              "guard targets over deep water");

        dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(!vp.valid && !fp.valid && dir.SeparationGuardCount() == 1, "guard does not re-issue every tick");
        int reissues = 0;
        for (int t = 0; t < 100; ++t) {                   // 5 s still too close: re-issued on its cooldown
            dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
            if (vp.valid) ++reissues;
        }
        Check(reissues >= 2 && reissues <= 5, "guard re-issues on its 1.5 s cooldown while still close");

        // Crossed over (Venturer East of Fenrir): routed around, not back through.
        dir.UpdateFlagshipPositions(300.0f, 270.0f, 240.0f, 272.0f);
        vp = {}; fp = {};
        for (int t = 0; t < 40 && !vp.valid; ++t) dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(vp.valid && fp.valid, "guard fires for crossed-over ships");
        Check(vp.y == kCeCcLaneMinY && fp.y == kCeCcLaneMaxY,
              "crossed-over ships are routed South about and North about");
        Check(std::fabs(vp.y - fp.y) >= 60.0f, "crossed-over routes diverge on Y");

        // A destroyed flagship or unknown positions silence the guard.
        dir.ClearFlagshipPositions();
        const int before = dir.SeparationGuardCount();
        for (int t = 0; t < 100; ++t) dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(dir.SeparationGuardCount() == before, "guard quiet without positions");
    }

    // 5. The hard push: only inside the hard distance, equal and opposite, capped,
    //    always increasing the gap, and finite for coincident centres.
    {
        CeCarrierClashSeparationPush none = CeCarrierClashComputeSeparationPush(
            180.0f, 262.0f, 340.0f, 266.0f, kCeCcHardSeparationDist, 0.6f);
        Check(!none.push, "no push at lane distance");
        CeCarrierClashSeparationPush pp = CeCarrierClashComputeSeparationPush(
            240.0f, 270.0f, 290.0f, 270.0f, kCeCcHardSeparationDist, 0.6f);
        Check(pp.push && pp.venturerDx < 0.0f && pp.fenrirDx > 0.0f, "push parts the hulls along their line");
        Check(std::fabs(pp.venturerDx + pp.fenrirDx) < 1e-5f && std::fabs(pp.venturerDy + pp.fenrirDy) < 1e-5f,
              "push is equal and opposite");
        Check(std::fabs(pp.fenrirDx) <= 0.6f + 1e-5f, "push is capped per tick");
        CeCarrierClashSeparationPush same = CeCarrierClashComputeSeparationPush(
            260.0f, 270.0f, 260.0f, 270.0f, kCeCcHardSeparationDist, 0.6f);
        Check(same.push && same.venturerDx < 0.0f && same.fenrirDx > 0.0f &&
              same.venturerDx == same.venturerDx && same.fenrirDy == same.fenrirDy,
              "coincident centres part along X with finite values");
        // Iterating the push from a full overlap always ends at the hard distance.
        float vx = 262.0f, vy = 270.0f, fx = 258.0f, fy = 271.0f;
        for (int t = 0; t < 400; ++t) {
            const CeCarrierClashSeparationPush q = CeCarrierClashComputeSeparationPush(
                vx, vy, fx, fy, kCeCcHardSeparationDist, 0.6f);
            if (!q.push) break;
            vx += q.venturerDx; vy += q.venturerDy; fx += q.fenrirDx; fy += q.fenrirDy;
        }
        Check(PlanarDist(vx, vy, fx, fy) >= kCeCcHardSeparationDist - 0.01f,
              "repeated pushes restore the hard separation");
        Check(!CeCarrierClashComputeSeparationPush(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.6f).push &&
              !CeCarrierClashComputeSeparationPush(0.0f, 0.0f, 1.0f, 0.0f, 100.0f, 0.0f).push,
              "degenerate push parameters are refused");
    }
}

// Three acts keyed to Fenrir's hull, the banner and bar feedback that announce
// them, the escort step-up, and the held level result.
static void TestBattleArcAndOutcomeHold() {
    using namespace CeCarrierClashWorld;
    Check(CeCarrierClashActForHull(100) == 0 && CeCarrierClashActForHull(76) == 0 &&
          CeCarrierClashActForHull(75) == 1 && CeCarrierClashActForHull(51) == 1 &&
          CeCarrierClashActForHull(50) == 2 && CeCarrierClashActForHull(26) == 2 &&
          CeCarrierClashActForHull(25) == 3 && CeCarrierClashActForHull(1) == 3,
          "acts step at 75 / 50 / 25");
    for (int act = 0; act <= kCeCcActCount; ++act) {
        Check(CeCarrierClashIsFontSmallSafeText(CeCarrierClashActBanner(act)), "act banner FONT_SMALL-safe");
        Check(std::strlen(CeCarrierClashActBanner(act)) < 48u, "act banner fits its box");
    }
    Check(std::strstr(CeCarrierClashActBanner(1), "ACT I ") && std::strstr(CeCarrierClashActBanner(2), "ACT II ") &&
          std::strstr(CeCarrierClashActBanner(3), "ACT III "), "three numbered acts");

    const CeCarrierClashWaveCaps base = CeCarrierClashDefaultCaps();
    CeCarrierClashWaveCaps prev = CeCarrierClashCapsForAct(base, 0);
    Check(prev.maxMuspellFighters == base.maxMuspellFighters && prev.maxForsetiFighters == base.maxForsetiFighters &&
          prev.waveIntervalSec == base.waveIntervalSec, "act 0 is the base caps");
    for (int act = 1; act <= kCeCcActCount; ++act) {
        const CeCarrierClashWaveCaps c = CeCarrierClashCapsForAct(base, act);
        Check(c.maxMuspellFighters > prev.maxMuspellFighters, "each act raises the Muspell escort ceiling");
        Check(c.maxForsetiFighters >= prev.maxForsetiFighters, "Forseti escort ceiling never drops");
        Check(c.waveIntervalSec < prev.waveIntervalSec && c.waveIntervalSec >= c.minSpawnCooldownSec,
              "each act shortens the wave interval, never below the spawn cooldown");
        Check(c.maxWorldUnits == base.maxWorldUnits, "world unit ceiling never moves");
        prev = c;
    }
    Check(prev.maxMuspellFighters + prev.maxForsetiFighters + 16 < base.maxWorldUnits,
          "final act escorts stay well inside the world ceiling");

    const CeEntityHandle hV = CeEntityHandlePack(3u, 7u), hF = CeEntityHandlePack(4u, 9u);
    CeCarrierClashDirector dir;
    dir.Arm();
    dir.BeginLevel();
    dir.OnUnitSpawn(hV, kVenturerUnit, kAllegianceForseti, 1000.0f, 1000.0f);
    dir.OnUnitSpawn(hF, kFenrirUnit, kAllegianceMuspell, 1000.0f, 1000.0f);
    CeCarrierClashSpawnOrder spawns[4] = {};
    int n = 0;
    CeCarrierClashPatrolOrder vp = {}, fp = {};

    CeCarrierClashHudState hud = dir.BuildHudState();
    Check(hud.battleAct == 0 && hud.showBanner && std::strstr(hud.bannerText, "BROADSIDE") != nullptr,
          "the battle opens with its own banner");
    for (int t = 0; t < 120; ++t) dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
    hud = dir.BuildHudState();
    Check(!hud.showBanner && hud.fenrirFlash == 0.0f && hud.venturerFlash == 0.0f,
          "opening banner clears after its time; bars are steady");

    static const float kLife[3] = {740.0f, 490.0f, 240.0f};
    static const char *const kTag[3] = {"ACT I ", "ACT II ", "ACT III "};
    for (int a = 0; a < 3; ++a) {
        dir.UpdateFlagshipTelemetry(1000.0f, 1000.0f, true, kLife[a], 1000.0f, true);
        hud = dir.BuildHudState();
        Check(dir.BattleAct() == a + 1 && hud.battleAct == a + 1, "act advances on Fenrir threshold");
        Check(hud.showBanner && std::strstr(hud.bannerText, kTag[a]) != nullptr &&
              CeCarrierClashIsFontSmallSafeText(hud.bannerText), "act banner shown on the crossing");
        Check(hud.fenrirFlash > 0.9f && hud.venturerFlash == 0.0f, "Fenrir bar flashes on its own crossing only");
        Check(dir.EffectiveCaps().maxMuspellFighters == base.maxMuspellFighters + a + 1,
              "director uses the act's escort ceiling");
        float lastFlash = hud.fenrirFlash;
        for (int t = 0; t < 30; ++t) {
            dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
            const float f = dir.BuildHudState().fenrirFlash;
            Check(f <= lastFlash, "flash only decays");
            lastFlash = f;
        }
        Check(dir.BuildHudState().fenrirFlash == 0.0f, "flash is over inside 1.5 s");
        Check(dir.BuildHudState().showBanner, "act banner still up after the flash");
        for (int t = 0; t < 90; ++t) dir.Tick(0.05f, 20, spawns, 4, &n, &vp, &fp);
        Check(!dir.BuildHudState().showBanner, "act banner clears after 4.5 s");
    }
    // Repairs never step an act back, and re-crossing does not re-announce it.
    dir.UpdateFlagshipTelemetry(1000.0f, 1000.0f, true, 900.0f, 1000.0f, true);
    Check(dir.BattleAct() == 3, "acts never step back");
    dir.UpdateFlagshipTelemetry(1000.0f, 1000.0f, true, 200.0f, 1000.0f, true);
    Check(!dir.BuildHudState().showBanner && dir.BuildHudState().fenrirFlash == 0.0f,
          "a reached act is not announced twice");
    // A skipped threshold (one huge hit) lands on the right act with one banner.
    {
        CeCarrierClashDirector d2;
        d2.Arm(); d2.BeginLevel();
        d2.OnUnitSpawn(hV, kVenturerUnit, kAllegianceForseti, 1000.0f, 1000.0f);
        d2.OnUnitSpawn(hF, kFenrirUnit, kAllegianceMuspell, 1000.0f, 1000.0f);
        d2.UpdateFlagshipTelemetry(1000.0f, 1000.0f, true, 300.0f, 1000.0f, true);
        Check(d2.BattleAct() == 2 && std::strstr(d2.BuildHudState().bannerText, "ACT II ") != nullptr,
              "a hit across two thresholds lands on the later act");
    }
    // Venturer thresholds flash its own bar; the last one warns in words.
    dir.UpdateFlagshipTelemetry(700.0f, 1000.0f, true, 200.0f, 1000.0f, true);
    hud = dir.BuildHudState();
    Check(hud.venturerFlash > 0.9f && hud.fenrirFlash == 0.0f && !hud.showBanner,
          "Venturer bar flashes at 75% without a banner");
    dir.UpdateFlagshipTelemetry(200.0f, 1000.0f, true, 200.0f, 1000.0f, true);
    hud = dir.BuildHudState();
    Check(hud.showBanner && std::strstr(hud.bannerText, "VENTURER HULL CRITICAL") != nullptr &&
          CeCarrierClashIsFontSmallSafeText(hud.bannerText), "Venturer critical warning shown");

    // Low-hull pulse: off at and above 35% and for a dead hull, bounded and moving below it.
    Check(CeCarrierClashLowHullPulse(35, 1.0f) == 0.0f && CeCarrierClashLowHullPulse(100, 2.0f) == 0.0f &&
          CeCarrierClashLowHullPulse(0, 3.0f) == 0.0f, "no pulse at 35% and above, or when destroyed");
    float lo = 2.0f, hi = -1.0f;
    for (int t = 0; t < 200; ++t) {
        const float v = CeCarrierClashLowHullPulse(34, 0.01f * float(t));
        Check(v >= 0.0f && v <= 1.0f, "pulse stays in [0..1]");
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    Check(lo < 0.1f && hi > 0.9f, "pulse swings fully below 35%");

    // Outcome hold: the phase is Victory at once, the level result waits ~4 s.
    Check(!dir.TickOutcomeHold(0.05f) && dir.OutcomeHoldElapsedSec() == 0.0f, "no hold while the battle runs");
    dir.OnUnitDestroyed(hF);
    Check(dir.Phase() == CE_CC_VICTORY, "Victory is instant");
    hud = dir.BuildHudState();
    Check(hud.showBanner && std::strstr(hud.bannerText, "VICTORY") != nullptr && hud.fenrirPct == 0,
          "Victory banner replaces any act banner at once");
    Check(dir.OutcomeHoldSec() == kCeCcVictoryHoldSec && kCeCcVictoryHoldSec >= 3.5f && kCeCcVictoryHoldSec <= 5.0f,
          "victory hold is about four seconds");
    int holdTicks = 0;
    bool fired = false;
    for (int t = 0; t < 400 && !fired; ++t) {
        fired = dir.TickOutcomeHold(0.05f);
        ++holdTicks;
        Check(dir.Phase() == CE_CC_VICTORY, "phase stays Victory through the hold");
    }
    Check(fired && holdTicks >= 78 && holdTicks <= 82, "level result released after ~4.0 s of ticks");
    Check(!dir.TickOutcomeHold(-1.0f) && !dir.TickOutcomeHold(9.0f), "bad dt never releases the hold");
    dir.MarkOutcomeDeclared();
    Check(!dir.TickOutcomeHold(0.05f), "hold releases once");

    // Venturer loss: Defeat at once, a shorter hold.
    dir.BeginLevel();
    dir.OnUnitSpawn(hV, kVenturerUnit, kAllegianceForseti, 1000.0f, 1000.0f);
    dir.OnUnitSpawn(hF, kFenrirUnit, kAllegianceMuspell, 1000.0f, 1000.0f);
    Check(dir.BattleAct() == 0 && dir.OutcomeHoldElapsedSec() == 0.0f && !dir.OutcomeDeclared(),
          "a new level resets the arc and the hold");
    dir.OnUnitDestroyed(hV);
    Check(dir.Phase() == CE_CC_DEFEAT && dir.OutcomeHoldSec() == kCeCcDefeatHoldSec, "Defeat hold armed");
    holdTicks = 0; fired = false;
    for (int t = 0; t < 400 && !fired; ++t) { fired = dir.TickOutcomeHold(0.05f); ++holdTicks; }
    Check(fired && holdTicks >= 48 && holdTicks <= 52, "defeat released after ~2.5 s");
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
    TestArchipelagoSculpting();
    TestWorldSerializerAndForbiddenUnits();
    TestPreflightMatrix();
    TestDirectorLifecycleAndCaps();
    TestBroadsideLanesAndSeparationGuard();
    TestBattleArcAndOutcomeHold();
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
            # Archipelago sculpting, sunset lighting and mixer alpha proof mutants
            ("pad-too-small", "ce_carrier_clash_world.h",
             "constexpr float kBastionPadRadius = 10.0f;",
             "constexpr float kBastionPadRadius = 6.0f;"),
            ("stack-behind-venturer", "ce_carrier_clash_world.h",
             "constexpr float kStackX = 280.0f;",
             "constexpr float kStackX = 150.0f;"),
            ("cove-filled", "ce_carrier_clash_world.h",
             "constexpr float kCoveFloorElev = -3.2f;",
             "constexpr float kCoveFloorElev = 9.0f;"),
            ("no-cast-shadows", "ce_carrier_clash_world.h",
             "return LightFromSunDot(lit * SunOpenAt(x, y, sunX, sunY, sunZ));",
             "return LightFromSunDot(lit);"),
            ("sun-wrong-side", "ce_carrier_clash_world.h",
             "constexpr float kSunDirY = 0.838f;",
             "constexpr float kSunDirY = -0.838f;"),
            ("mixer-wrap-guard-off", "ce_carrier_clash_world.h",
             "if (out.rock < rockFloor) out.rock = rockFloor;",
             "(void)rockFloor;"),
            ("mixer-alpha-out-of-range", "ce_carrier_clash_world.h",
             "constexpr int kMixAlphaMax = 62;",
             "constexpr int kMixAlphaMax = 90;"),
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
            # Broadside lanes, live separation guard, battle arc and outcome hold mutants
            ("lanes-too-close", "ce_carrier_clash.h",
             "{340.0f, 266.0f, kCeCcFenrirZ},",
             "{282.0f, 266.0f, kCeCcFenrirZ},"),
            ("no-altitude-stagger", "ce_carrier_clash.h",
             "constexpr float kCeCcFenrirZ = -50.0f;",
             "constexpr float kCeCcFenrirZ = -34.0f;"),
            ("guard-never-fires", "ce_carrier_clash.h",
             "if (dx * dx + dy * dy < kCeCcSeparationGuardDist * kCeCcSeparationGuardDist) {",
             "if (false && dx * dx + dy * dy < kCeCcSeparationGuardDist * kCeCcSeparationGuardDist) {"),
            ("guard-same-side", "ce_carrier_clash.h",
             "if (outFenrirPatrol) *outFenrirPatrol = {true, kCeCcEastLaneCentreX, fy, kCeCcFenrirZ};",
             "if (outFenrirPatrol) *outFenrirPatrol = {true, kCeCcWestLaneCentreX, fy, kCeCcFenrirZ};"),
            ("guard-no-cooldown", "ce_carrier_clash.h",
             "separationCooldownSec_ = kCeCcSeparationReissueSec;",
             "separationCooldownSec_ = 0.0f;"),
            ("guard-crossed-through", "ce_carrier_clash.h",
             "if (dx < 0.0f) {\n\t\t\t\t\tvy = kCeCcLaneMinY;",
             "if (false) {\n\t\t\t\t\tvy = kCeCcLaneMinY;"),
            ("push-wrong-way", "ce_carrier_clash.h",
             "out.venturerDx = -nx * step;",
             "out.venturerDx = nx * step;"),
            ("push-uncapped", "ce_carrier_clash.h",
             "if (step > maxStep) step = maxStep;",
             "(void)maxStep;"),
            ("opening-stations-old", "ce_carrier_clash_world.h",
             "constexpr float kFenrirStartX = 340.0f;",
             "constexpr float kFenrirStartX = 304.0f;"),
            ("act-steps-back", "ce_carrier_clash.h",
             "if (act > battleAct_) {",
             "if (act != battleAct_) {"),
            ("act-wrong-threshold", "ce_carrier_clash.h",
             "if (fenrirPct <= 50) return 2;",
             "if (fenrirPct <= 40) return 2;"),
            ("act-no-escort-step-up", "ce_carrier_clash.h",
             "caps.maxMuspellFighters = base.maxMuspellFighters + act;",
             "caps.maxMuspellFighters = base.maxMuspellFighters;"),
            ("no-hull-flash", "ce_carrier_clash.h",
             "battleAct_ = act;\n\t\t\t\tfenrirFlashSec_ = kCeCcHullFlashSec;",
             "battleAct_ = act;"),
            ("victory-not-held", "ce_carrier_clash.h",
             "return outcomeHoldSec_ + 1e-4f >= OutcomeHoldSec();",
             "return true;"),
            ("pulse-always-on", "ce_carrier_clash.h",
             "if (pct <= 0 || pct >= kCeCcLowHullPct) return 0.0f;",
             "if (pct <= 0) return 0.0f;"),
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

        # One sunset vector lights the engine sun, the terrain lightmap and the
        # host driver; the lightmap casts shadows and the mixer alphas are the
        # header's proven ones.
        for axis in "XYZ":
            self.assertIn(f"MAP.mHFP.SunPos{axis} = CeCarrierClashWorld::kSunDir{axis};", cc_world_cpp)
        self.assertIn("CeCarrierClashWorld::ComputeCellLightShadowed(x, y, sunX, sunY, sunZ)", cc_world_cpp)
        self.assertIn("CeCarrierClashWorld::ComputeMixAlphas(wx, wy)", cc_world_cpp)

        # The opening banner names Carrier Clash as a CE original: drawn in place
        # of the director's opening line, FONT_SMALL-safe, and no longer than the
        # longest banner the box already carries.
        title = re.search(r'const char kOpeningTitle\[\] = "([^"]*)";', cc_cpp)
        self.assertIsNotNone(title, "opening title literal missing")
        title = title.group(1)
        self.assertIn("CARRIER CLASH", title)
        self.assertIn("CE ORIGINAL", title)
        self.assertTrue(all(32 <= ord(ch) < 127 and ch not in "[]|_~\\$@^{}" for ch in title),
                        "opening title uses a glyph FONT_SMALL lacks")
        self.assertLessEqual(len(title), len("F.S. VENTURER HULL CRITICAL - DEFEND THE FLAGSHIP"))
        self.assertIn("std::strcmp(state.bannerText, CeCarrierClashActBanner(0)) == 0", cc_cpp)

        # Live separation, held level result, overlay outcome and symmetric device state.
        self.assertIn("gDirector.UpdateFlagshipPositions(", cc_cpp)
        self.assertIn("EnforceFlagshipSeparation(venturer, fenrir);", cc_cpp)
        self.assertIn("CeCarrierClashComputeSeparationPush(", cc_cpp)
        self.assertIn("gDirector.TickOutcomeHold(CLOCK_TICK)", cc_cpp)
        tick_fn = cc_cpp[cc_cpp.index('extern "C" void PortCeCarrierClashTickPre(void)'):
                         cc_cpp.index("bool ShouldRenderCarrierClashHud(")]
        self.assertLess(tick_fn.index("gDirector.TickOutcomeHold(CLOCK_TICK)"),
                        tick_fn.index("GAME.DeclareLevelWon();"),
                        "the level result must wait for the outcome hold")
        self.assertEqual(tick_fn.count("GAME.DeclareLevelWon();"), 1)
        self.assertIn("if (endState != overlayPass) return;", cc_cpp)
        self.assertNotIn("gHudDrawnThisFrame", cc_cpp)
        render_fn = cc_cpp[cc_cpp.index("void RenderCarrierClashHud(bool overlayPass)"):
                           cc_cpp.index('extern "C" float PortCeCarrierClashHudTopBottom(')]
        self.assertIn("RestoreHudDeviceState();", render_fn)
        restore_fn = cc_cpp[cc_cpp.index("void RestoreHudDeviceState()"):cc_cpp.index("} // namespace", cc_cpp.index("void RestoreHudDeviceState()"))]
        for state in ("D3DRS_CULLMODE, D3DCULL_CCW", "D3DRS_ALPHATESTENABLE, TRUE", "D3DRS_ALPHABLENDENABLE, TRUE",
                      "D3DRS_SRCBLEND, D3DBLEND_SRCALPHA", "D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA",
                      "D3DRS_FOGENABLE, FALSE", "D3DRS_LIGHTING, FALSE", "D3DRS_ZENABLE, TRUE",
                      "D3DRS_ZWRITEENABLE, TRUE", "LT.SetModulate(0);"):
            self.assertIn(state, restore_fn, f"HUD restore misses {state}")


if __name__ == "__main__":
    unittest.main()
