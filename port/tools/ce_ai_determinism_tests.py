#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phase 4 Cross-Platform AI Determinism, Budget & Layer-3 Isolation Gate.

Proves:
  1. Zero libm imports in ce_multibe_ai.h + ce_ai_pilot_weights.h + ce_player_hooks.h + ce_ai_tactical_l2.cpp
     (compiled with pinned Linux i686 Clang -mno-sse -mfpmath=387 -O2 -fno-builtin and checked with llvm-nm -u).
  2. 10,000-tick rollout bit-identical 64-bit state-hash equality between pinned Linux i686 Clang and
     pinned Windows i686-w64-mingw32-clang++ (executed under Wine), including differing pre-match
     worldEpoch histories (epoch=1 vs epoch=77) -> 0 divergences!
  3. Layer-1 p99 compute budget < 0.1 ms/tick (< 100 us/tick) across 2,000 ticks.
  4. Layer-3 optional strategist isolation:
     - 0 common.h / MemoryManager / MEM_MANAGER / bea_game_new / rand() references in source & object;
     - 0 bundled L3 model weights (*.bin / *.gguf / *.onnx) in port/game/ai/;
     - graceful prior == nullptr and 0 main-thread stalls when external weights are absent;
     - valid sanitized printable ASCII hint (<= 160 chars) and 0 main-thread stalls when external
       strategist.bin is supplied in <user_dir>/ai-models/, plus clean reset/shutdown under ASan/UBSan.
"""

from __future__ import annotations

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
sys.path.insert(0, str(PORT))
import deps  # noqa: E402

LINUX_CLANGXX = deps.DATA / "llvm/bin/clang++"
LINUX_NM = deps.DATA / "llvm/bin/llvm-nm"
WIN_CLANGXX = deps.DATA / "llvm-mingw/bin/i686-w64-mingw32-clang++"

FORBIDDEN_LIBM = {
    "sin", "sinf", "cos", "cosf", "tan", "tanf", "atan", "atanf", "atan2", "atan2f",
    "sqrt", "sqrtf", "fabs", "fabsf", "pow", "powf", "exp", "expf", "log", "logf",
    "log10", "log10f", "asin", "asinf", "acos", "acosf", "floor", "floorf", "ceil", "ceilf",
    "fmod", "fmodf", "remainder", "remainderf", "hypot", "hypotf",
}

ROLLOUT_DRIVER = r"""
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ce_multibe_ai.h"
#include "ce_player_hooks.h"
#include "ai/ce_ai_pilot_l1.h"
#include "ai/ce_ai_tactical_l2.h"

static uint64_t FnvMixU32(uint64_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
    {
        h ^= (v >> (i * 8)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t FnvMixF32(uint64_t h, float f)
{
    uint32_t u = 0u;
    if (f == 0.0f) f = 0.0f; // canonicalize -0.0f
    memcpy(&u, &f, 4u);
    return FnvMixU32(h, u);
}

static uint64_t RunRollout(uint32_t worldEpoch, int ticks, uint64_t *outMlpAndGuardSumUs, uint64_t *outP99Ns)
{
    uint64_t h = 14695981039346656037ULL;
    CeMultiBeDamageState dmg;
    dmg.Reset(100.0f);
    CeMultiBeWalkerDashState dash;
    dash.Reset();
    CeAiTacticalL2State tac;
    CeAiTacticalL2Reset(&tac);

    CeAiJetPhase jetPhase = CE_AI_JET_CRUISE;
    int jetPhaseTicks = 0;
    int breakDir = 1;
    int strafeDir = 1;
    int strafeTicks = 0;
    bool heatPause = false;
    bool retreating = false;
    bool isJet = false;
    float px = 120.0f, py = 140.0f, pz = -12.0f;
    float yaw = 0.3f, pitch = 0.0f;

    static uint32_t latNs[2000];
    int latCount = 0;

    for (int t = 0; t < ticks; ++t)
    {
        const uint32_t rnd0 = CePlayerAiSeed(0xC0FFEE17u, 1u, static_cast<uint32_t>(t));
        const uint32_t rnd1 = CePlayerAiSeed(0xC0FFEE17u, 2u, static_cast<uint32_t>(t));
        const float now = static_cast<float>(t) * 0.05f;
        const float life = 25.0f + static_cast<float>(rnd0 % 75u);
        const float energy = 0.15f + static_cast<float>(rnd1 % 85u) * 0.01f;
        dmg.Update(life, 100.0f, (t % 17 == 0) ? now : -1.0f, now);

        // Build CePlayerObs with entity handles stamped with DIFFERENT worldEpoch histories!
        static CePlayerObs obs;
        memset(&obs, 0, sizeof(obs));
        obs.simTick = static_cast<uint32_t>(t + 1);
        obs.matchSeed = 0xC0FFEE17u;
        obs.slot = 1u;
        obs.isJet = isJet ? 1u : 0u;
        obs.isOnGround = isJet ? 0u : 1u;
        obs.allegiance = 1;
        obs.activeToggles = (t & 64) ? ((1u << 6) | (1u << 2) | (1u << 3)) : (1u << 6);
        obs.dryMask = CE_DRY_HERE | CE_DRY_FWD | CE_DRY_BACK | CE_DRY_LEFT | CE_DRY_RIGHT | CE_DRY_AHEAD_28;
        obs.lifeRatio = life * 0.01f;
        obs.energyRatio = energy;
        obs.pos[0] = px; obs.pos[1] = py; obs.pos[2] = pz;
        obs.yaw = yaw; obs.pitch = pitch;
        obs.groundZ = -10.0f;
        obs.waterZ = 0.0f;
        obs.altitude = obs.groundZ - pz;
        obs.weaponReady = ((t % 3) != 0) ? 1u : 0u;
        obs.weaponHeatOrAmmo = static_cast<float>(t % 20) * 0.04f;

        // Deliberately insert entities in reversed order with different worldEpoch bits in handle,
        // and ties on distSqQuantized broken solely by match-local spawnSeq!
        obs.entityCount = 8u;
        for (uint32_t e = 0u; e < 8u; ++e)
        {
            const uint32_t slotId = (e + worldEpoch * 7u) & 0xFFFFu;
            const uint32_t gen = (worldEpoch + e * 3u) & 0x7FFFu;
            obs.entities[e].handle = (gen << 16) | (slotId & 0xFFFFu);
            obs.entities[e].spawnSeq = 100u - e;
            const float dx = static_cast<float>(((e / 2u) + 1u) * 24u) * ((e & 1u) ? -1.0f : 1.0f);
            const float dy = static_cast<float>((t + e * 11u) % 40u) - 20.0f;
            obs.entities[e].pos[0] = px + dx;
            obs.entities[e].pos[1] = py + dy;
            obs.entities[e].pos[2] = -10.0f - static_cast<float>(e % 3u) * 6.0f;
            obs.entities[e].distSqQuantized = CePlayerObsQuantizeDistSq(dx * dx + dy * dy);
            obs.entities[e].isFriendly = (e == 5u) ? 1u : 0u;
            obs.entities[e].isNeutral = (e == 6u || e == 7u) ? 1u : 0u;
            obs.entities[e].isRepairPad = (e == 7u) ? 1u : 0u;
            obs.entities[e].airborne = (e % 3u == 0u) ? 1u : 0u;
            obs.entities[e].isBattleEngine = (e == 0u) ? 1u : 0u;
            obs.entities[e].targetClass = static_cast<uint8_t>(e % 7u);
            obs.entities[e].lifeRatio = 0.2f + static_cast<float>(e) * 0.1f;
        }
        CePlayerObsSortEntities(obs.entities, obs.entityCount);

        obs.squadCount = 2u;
        for (uint32_t s = 0u; s < 2u; ++s)
        {
            obs.squads[s].squadHandle = (worldEpoch << 16) | (s + 1u);
            obs.squads[s].ordinal = static_cast<int32_t>(s);
            obs.squads[s].friendly = 1u;
            obs.squads[s].commandable = 1u;
            obs.squads[s].members = 4;
            obs.squads[s].x = 200.0f + static_cast<float>(s * 40u);
            obs.squads[s].y = 220.0f;
            obs.squads[s].lifeRatio = 0.8f;
        }
        obs.takeableCount = 1u;
        obs.takeable[0].unitHandle = (worldEpoch << 16) | 42u;
        obs.takeable[0].spawnSeq = 55u;
        obs.takeable[0].x = 180.0f;
        obs.takeable[0].y = 190.0f;
        obs.takeable[0].z = -10.0f;
        obs.takeable[0].lifeRatio = 0.9f;

        std::chrono::steady_clock::time_point ts0{};
        const bool measure = (outP99Ns != NULL && t < 2000);
        if (measure) ts0 = std::chrono::steady_clock::now();

        // 1. Layer-1 feature extraction (+ optional INT8 MLP forward pass when CE_AI_PILOT_TRAINED_WEIGHTS is set)
        int16_t feat[kCeAiMlpInDim];
        int16_t mlpOut[kCeAiMlpOutDim];
        memset(mlpOut, 0, sizeof(mlpOut));
        CeAiPilotExtractFeaturesQ12(obs, NULL, feat);
#ifdef CE_AI_PILOT_TRAINED_WEIGHTS
        CeAiPilotMlpInfer(feat, mlpOut);
#endif

        // 2. Zero-libm guardrails & physics kinematics
        const CeAiVec3 myV = { px, py, pz };
        const CeAiVec3 tgtV = { obs.entities[0].pos[0], obs.entities[0].pos[1], obs.entities[0].pos[2] };
        const CeAiVec3 velV = { 8.0f, -6.0f, 1.5f };
        const CeAiVec3 lead = CeMultiBeComputeLeadPoint(myV, tgtV, velV, 110.0f, 2.2f);
        const CeAiVec3 toLead = { lead.x - px, lead.y - py, lead.z - pz };
        const float targetYaw = CeAiAzimuth(toLead.x, toLead.y);
        const float targetPitch = CeAiElevation(toLead.x, toLead.y, toLead.z);
        const float dist = CeAiSqrt(toLead.x * toLead.x + toLead.y * toLead.y + toLead.z * toLead.z);

        CeMultiBeSurvivalInput survIn = { obs.lifeRatio, obs.energyRatio, isJet, true, true, true, retreating };
        const CeMultiBeSurvivalDecision surv = CeMultiBeEvaluateSurvival(survIn);
        retreating = surv.retreating;

        CeMultiBeSmartMorphInput morphIn = {};
        morphIn.isJet = isJet;
        morphIn.flightAllowed = true;
        morphIn.groundIsDry = true;
        morphIn.aheadGroundIsDry = true;
        morphIn.energyRatio = obs.energyRatio;
        morphIn.hasHostile = true;
        morphIn.targetDist = dist;
        morphIn.targetAltitude = 4.0f;
        morphIn.goalDist = dist;
        morphIn.retreating = retreating;
        morphIn.ticksSinceMorph = (t % 160);
        morphIn.slotIndex = 1;
        morphIn.simTicks = t;
        morphIn.altitudeAboveGround = isJet ? 6.0f : 0.0f;
        morphIn.isOnGround = !isJet;
        const bool wantMorph = CeMultiBeShouldMorphSmart(morphIn);

        CeMultiBeWalkerMoveInput wIn = {};
        wIn.hasHostile = true;
        wIn.isRetreating = retreating;
        wIn.targetDist = dist;
        wIn.goalDist = dist;
        wIn.yawErrToGoal = CeAiWrapAngle(targetYaw - yaw);
        wIn.underFire = (dmg.underFireTicks > 0);
        wIn.heavyHit = dmg.heavyHitPending;
        wIn.strafeDir = strafeDir;
        wIn.strafeTicks = strafeTicks;
        wIn.dryFwd = wIn.dryBack = wIn.dryLeft = wIn.dryRight = wIn.dryDashLeft = wIn.dryDashRight = true;
        const CeMultiBeWalkerMoveCmd wCmd = CeMultiBeComputeWalkerMove(wIn);
        strafeDir = wCmd.nextStrafeDir;
        strafeTicks = wCmd.nextStrafeTicks;
        const CeMultiBeWalkerDashOutput dOut = CeMultiBeStepWalkerDash(dash, wCmd.wantDash, wCmd.dashDir, !isJet, 2, true, now);

        CeMultiBeJetFlightInput jIn = {};
        jIn.curYaw = yaw;
        jIn.curPitch = pitch;
        jIn.posZ = isJet ? -28.0f : -10.0f;
        jIn.groundZ = -10.0f;
        jIn.aheadGroundZ = -12.0f;
        jIn.waterZ = 0.0f;
        jIn.toGoal = toLead;
        jIn.hasHostile = true;
        jIn.isRetreating = retreating;
        jIn.underHeavyFire = dmg.heavyHitPending;
        jIn.desiredCruiseHeight = 22.0f + static_cast<float>(mlpOut[CE_AI_OUT_JET_CRUISE_BIAS]) * (2.0f / 4096.0f);
        jIn.prevPhase = jetPhase;
        jIn.phaseTicks = jetPhaseTicks;
        jIn.breakTurnDir = breakDir;
        jIn.groundIsDry = true;
        jIn.aheadGroundIsDry = true;
        const CeMultiBeJetFlightCmd jCmd = CeMultiBeComputeJetFlight(jIn);
        jetPhase = jCmd.nextPhase;
        jetPhaseTicks = jCmd.nextPhaseTicks;
        breakDir = jCmd.nextBreakTurnDir;

        CeMultiBeFireGateInput fIn = {};
        fIn.hasHostile = true;
        fIn.isJet = isJet;
        fIn.weaponActive = true;
        fIn.readyToFire = (obs.weaponReady != 0u);
        fIn.isEnergyWeapon = true;
        fIn.heatOrAmmoRatio = obs.weaponHeatOrAmmo;
        fIn.targetDist = dist;
        fIn.maxRange = 220.0f;
        fIn.yawErrAbs = CeAiAbs(CeAiWrapAngle(targetYaw - yaw));
        fIn.pitchErrAbs = CeAiAbs(CeAiWrapAngle(targetPitch - pitch));
        fIn.maxYawCone = 0.25f;
        fIn.maxPitchCone = 0.28f;
        fIn.heatCoolingPause = heatPause;
        const CeMultiBeFireDecision fDec = CeMultiBeEvaluateFireGate(fIn);
        heatPause = fDec.nextHeatPause;

        CeCommanderIntent cmdIntent = {};
        CePossessIntent posIntent = {};
        CeAiTacticalL2Decide(&tac, obs, NULL, &cmdIntent, &posIntent);

        if (measure)
        {
            const auto dt = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - ts0).count());
            latNs[latCount++] = static_cast<uint32_t>(dt > 0xFFFFFFFFu ? 0xFFFFFFFFu : dt);
            if (outMlpAndGuardSumUs) *outMlpAndGuardSumUs += dt / 1000ULL;
        }

        // Advance synthetic state deterministically
        yaw = CeAiWrapAngle(yaw + CeAiSin(targetYaw - yaw) * 0.1f + CeAiCos(pitch) * 0.01f);
        pitch = CeAiClamp(pitch + CeAiClamp(CeAiWrapAngle(targetPitch - pitch) * 1.4f, -0.9f, 0.9f) * 0.05f, -0.5f, 0.5f);
        px = CeAiClamp(px + wCmd.strafeInput * 0.4f - wCmd.forwardInput * 0.5f, 60.0f, 450.0f);
        py = CeAiClamp(py + CeAiCos(yaw) * 0.5f, 60.0f, 450.0f);
        if (wantMorph) isJet = !isJet;
        pz = isJet ? -26.0f : -10.0f;

        // Mix every decision output (strictly EXCLUDING raw worldEpoch/handle bits!) into state hash
        for (int k = 0; k < kCeAiMlpInDim; ++k)
            h = FnvMixU32(h, static_cast<uint32_t>(static_cast<uint16_t>(feat[k])));
        for (int k = 0; k < kCeAiMlpOutDim; ++k)
            h = FnvMixU32(h, static_cast<uint32_t>(static_cast<uint16_t>(mlpOut[k])));
        for (uint32_t e = 0u; e < obs.entityCount; ++e)
        {
            h = FnvMixU32(h, obs.entities[e].spawnSeq);
            h = FnvMixU32(h, obs.entities[e].distSqQuantized);
        }
        h = FnvMixF32(h, lead.x);
        h = FnvMixF32(h, lead.y);
        h = FnvMixF32(h, lead.z);
        h = FnvMixF32(h, targetYaw);
        h = FnvMixF32(h, targetPitch);
        h = FnvMixF32(h, wCmd.forwardInput);
        h = FnvMixF32(h, wCmd.strafeInput);
        h = FnvMixF32(h, dOut.strafeValue);
        h = FnvMixF32(h, jCmd.turnInput);
        h = FnvMixF32(h, jCmd.pitchInput);
        h = FnvMixF32(h, jCmd.thrustVy);
        h = FnvMixU32(h, static_cast<uint32_t>(jCmd.nextPhase));
        h = FnvMixU32(h, static_cast<uint32_t>(fDec.action));
        h = FnvMixU32(h, wantMorph ? 1u : 0u);
        h = FnvMixU32(h, static_cast<uint32_t>(cmdIntent.valid) | (static_cast<uint32_t>(cmdIntent.verb) << 4));
        h = FnvMixF32(h, cmdIntent.goalX);
        h = FnvMixU32(h, static_cast<uint32_t>(posIntent.valid) | (static_cast<uint32_t>(posIntent.fire) << 4));
        h = FnvMixF32(h, posIntent.goalX);
    }

    if (outP99Ns && latCount > 0)
    {
        // Insertion sort small latency array to get exact p99.
        for (int i = 1; i < latCount; ++i)
        {
            uint32_t key = latNs[i];
            int j = i - 1;
            while (j >= 0 && latNs[j] > key)
            {
                latNs[j + 1] = latNs[j];
                --j;
            }
            latNs[j + 1] = key;
        }
        *outP99Ns = latNs[(latCount * 99) / 100];
    }
    return h;
}

int main()
{
    uint64_t sumUs = 0, p99Ns = 0;
    const uint64_t h1 = RunRollout(1u, 10000, &sumUs, &p99Ns);
    const uint64_t h77 = RunRollout(77u, 10000, NULL, NULL);
    printf("HASH_EPOCH1=%016llx\n", static_cast<unsigned long long>(h1));
    printf("HASH_EPOCH77=%016llx\n", static_cast<unsigned long long>(h77));
    printf("P99_NS=%llu\n", static_cast<unsigned long long>(p99Ns));
    printf("MEAN_US=%.2f\n", static_cast<double>(sumUs) / 2000.0);
    return (h1 == h77) ? 0 : 2;
}
"""

L3_DRIVER = r"""
#include <assert.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "ai/ce_ai_strategist_l3.h"

static std::string gTestUserDir;
const char *PortUserDir() { return gTestUserDir.c_str(); }

static CePlayerObs MakeObs(uint32_t tick, float life)
{
    CePlayerObs obs{};
    obs.simTick = tick;
    obs.slot = 0u;
    obs.lifeRatio = life;
    obs.energyRatio = 0.8f;
    obs.pos[0] = 200.0f; obs.pos[1] = 210.0f; obs.pos[2] = -10.0f;
    obs.entityCount = 3u;
    // Entity 0: real hostile airborne Battle Engine
    obs.entities[0].isFriendly = 0u;
    obs.entities[0].isNeutral = 0u;
    obs.entities[0].isBattleEngine = 1u;
    obs.entities[0].airborne = 1u;
    obs.entities[0].pos[0] = 280.0f; obs.entities[0].pos[1] = 260.0f; obs.entities[0].pos[2] = -25.0f;
    obs.entities[0].distSqQuantized = 12000u;
    // Entity 1: independent Repair Pad (must be usable for repair and NEVER counted as hostile!)
    obs.entities[1].isFriendly = 0u;
    obs.entities[1].isNeutral = 1u;
    obs.entities[1].isRepairPad = 1u;
    obs.entities[1].pos[0] = 140.0f; obs.entities[1].pos[1] = 150.0f; obs.entities[1].pos[2] = -10.0f;
    // Entity 2: neutral non-hostile world unit (must NEVER be counted as hostile!)
    obs.entities[2].isFriendly = 0u;
    obs.entities[2].isNeutral = 1u;
    obs.entities[2].isRepairPad = 0u;
    obs.entities[2].pos[0] = 190.0f; obs.entities[2].pos[1] = 195.0f; obs.entities[2].pos[2] = -10.0f;
    return obs;
}

int main(int argc, char **argv)
{
    assert(argc >= 3);
    const std::string emptyDir = argv[1];
    const std::string modelDir = argv[2];

    // 1. Weights absent (default package): Poll returns nullptr, zero stalls.
    gTestUserDir = emptyDir;
    PortCeAiStrategistLevelReset();
    CePlayerObs obs = MakeObs(1u, 0.85f);
    auto t0 = std::chrono::steady_clock::now();
    for (uint32_t i = 0u; i < 200u; ++i)
    {
        obs.simTick = i + 1u;
        PortCeAiStrategistPublishObs(0u, &obs);
        const CeStrategicPrior *p = PortCeAiStrategistPoll(0u);
        assert(p == nullptr);
    }
    auto absentUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    assert(absentUs < 50000); // 200 calls far under 50ms total

    // 2. External optional weights present with dirty control characters in label.
    gTestUserDir = modelDir;
    PortCeAiStrategistLevelReset();
    CePlayerObs lowHp = MakeObs(10u, 0.25f);
    auto t1 = std::chrono::steady_clock::now();
    PortCeAiStrategistPublishObs(0u, &lowHp);
    auto pubUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t1).count();
    assert(pubUs < 50000); // main thread publish never stalls

    const CeStrategicPrior *prior = nullptr;
    for (int wait = 0; wait < 100 && !prior; ++wait)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        prior = PortCeAiStrategistPoll(0u);
    }
    assert(prior != nullptr);
    assert(prior->posture == CE_STRAT_REPAIR);
    assert(prior->hasFocusPoint == 1u);
    assert(prior->focusX == 140.0f && prior->focusY == 150.0f);
    assert(std::strstr(prior->hint, "hostiles=1") != nullptr);
    const size_t hlen = std::strlen(prior->hint);
    assert(hlen > 0u && hlen <= 160u);
    for (size_t i = 0u; i < hlen; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(prior->hint[i]);
        assert(c >= 0x20u && c <= 0x7Eu);
    }

    // 3. Direct sanitization torture test (oversized + control chars).
    char dirty[256];
    for (int i = 0; i < 220; ++i)
        dirty[i] = (i % 7 == 0) ? '\x01' : ((i % 11 == 0) ? '\n' : 'A');
    dirty[220] = '\0';
    char clean[164];
    CeAiStrategistSanitizeHint(dirty, clean);
    const size_t clen = std::strlen(clean);
    assert(clen <= 160u);
    for (size_t i = 0u; i < clen; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(clean[i]);
        assert(c >= 0x20u && c <= 0x7Eu);
    }

    // Rapid concurrent publish/poll stress to verify wait-free drop-if-busy and non-blocking Poll.
    for (uint32_t k = 0u; k < 50u; ++k)
    {
        lowHp.simTick = 100u + k;
        PortCeAiStrategistPublishObs(0u, &lowHp);
        const CeStrategicPrior *q = PortCeAiStrategistPoll(0u);
        assert(q != nullptr);
    }

    const std::string savedHint(prior->hint);

    // 4. Clean reset & shutdown.
    PortCeAiStrategistLevelReset();
    assert(PortCeAiStrategistPoll(0u) == nullptr);
    PortCeAiStrategistShutdown();
    printf("L3_OK hint=%s\n", savedHint.c_str());
    return 0;
}
"""


def _parse_kv(stdout: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


class CeAiDeterminismTests(unittest.TestCase):
    def test_01_zero_libm_and_zero_bundled_l3_weights(self):
        # Verify zero bundled L3 model weights in repo.
        ai_dir = PORT / "game/ai"
        bundled_models = [p for p in ai_dir.rglob("*") if p.suffix.lower() in {".bin", ".gguf", ".onnx", ".pt", ".safetensors"}]
        self.assertEqual(bundled_models, [], f"Base package must ship 0 L3 weights, found: {bundled_models}")

        # Static source check: zero libm headers/calls in ce_multibe_ai.h, ce_ai_pilot_l1.cpp, ce_ai_tactical_l2.cpp.
        for rel in ("port/game/ce_multibe_ai.h", "port/game/ai/ce_ai_pilot_l1.cpp",
                    "port/game/ai/ce_ai_tactical_l2.cpp", "port/game/ai/ce_ai_pilot_weights.h"):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertNotIn("<math.h>", text, f"{rel} must not include <math.h>")
            self.assertNotIn("<cmath>", text, f"{rel} must not include <cmath>")
            for fn in ("sqrtf(", "fabsf(", "sinf(", "cosf(", "atan2f(", "atanf(", "powf("):
                self.assertNotIn(fn, text, f"{rel} must not call {fn}")

        # Object-level symbol & weight-bloat check with pinned Linux i686 Clang in BOTH modes:
        #   1) Default production compile: zero libm AND zero 107.5 KB untrained weight table bloat.
        #   2) -DCE_AI_PILOT_TRAINED_WEIGHTS: zero libm when the INT8 MLP inference engine is enabled.
        self.assertTrue(LINUX_CLANGXX.is_file(), f"Pinned Linux clang++ missing at {LINUX_CLANGXX}")
        with tempfile.TemporaryDirectory(prefix="ce_ai_nolibm_") as td:
            src = Path(td) / "check_nolibm.cpp"
            src.write_text(
                '#include "ce_multibe_ai.h"\n'
                '#include "ce_multibe_state.h"\n'
                '#include "ai/ce_ai_pilot_l1.h"\n'
                '#include "ai/ce_ai_tactical_l2.cpp"\n'
                'float probe(float x, float y, float z, const CePlayerObs &obs) {\n'
                '  int16_t feat[kCeAiMlpInDim];\n'
                '  CeAiPilotExtractFeaturesQ12(obs, nullptr, feat);\n'
                '  int16_t out[kCeAiMlpOutDim] = {};\n'
                '#ifdef CE_AI_PILOT_TRAINED_WEIGHTS\n'
                '  CeAiPilotMlpInfer(feat, out);\n'
                '#endif\n'
                '  CeAiVec3 a = {x, y, z}, b = {x + 20.f, y - 15.f, z}, v = {2.f, 3.f, 0.f};\n'
                '  CeAiVec3 p = CeMultiBeComputeLeadPoint(a, b, v, 80.f, 2.f);\n'
                '  return CeAiSqrt(p.x*p.x + p.y*p.y) + CeAiSin(CeAiAzimuth(p.x, p.y)) + CeAiCos(CeAiElevation(p.x, p.y, p.z))\n'
                '       + CeMultiBeWrapAngle(x) + CeMultiBeComputeWalkerPitchInput(y, z) + float(feat[0] + out[0]);\n'
                '}\n',
                encoding="utf-8",
            )
            default_obj = Path(td) / "check_nolibm_default.o"
            trained_obj = Path(td) / "check_nolibm_trained.o"
            for label, obj, extra in (
                ("default", default_obj, []),
                ("trained_weights", trained_obj, ["-DCE_AI_PILOT_TRAINED_WEIGHTS"]),
            ):
                with self.subTest(mode=label):
                    cmd = [
                        str(LINUX_CLANGXX), "--target=i686-pc-linux-gnu",
                        *deps.sysroot_flags(),
                        "-std=c++17", "-mno-sse", "-mfpmath=387", "-O2", "-fno-builtin",
                        *extra,
                        f"-I{PORT / 'game'}", "-c", str(src), "-o", str(obj),
                    ]
                    subprocess.run(cmd, check=True)
                    nm = subprocess.run([str(LINUX_NM), "-u", str(obj)], capture_output=True, text=True, check=True)
                    undef = {line.split()[-1].lstrip("_") for line in nm.stdout.splitlines() if line.strip()}
                    bad = sorted(undef & FORBIDDEN_LIBM)
                    self.assertEqual(bad, [], f"Unexpected libm imports in {label} AI object: {bad}")
                    if label == "default":
                        nm_def = subprocess.run([str(LINUX_NM), "--defined-only", str(obj)], capture_output=True, text=True, check=True)
                        for wsym in ("kCeAiMlpW1", "kCeAiMlpW2", "kCeAiMlpW3", "kCeAiMlpW4"):
                            self.assertNotIn(wsym, nm_def.stdout, f"Default build must not embed untrained {wsym}")
                        self.assertLess(obj.stat().st_size, 32_768, f"Default AI object bloated ({obj.stat().st_size} bytes)")
                    else:
                        self.assertGreater(obj.stat().st_size, default_obj.stat().st_size + 100_000,
                                           "Trained-weights object should embed the 107.5 KB INT8 weight matrix")

            # Also inspect the live compiled ce_ai_pilot_l1.cpp.o when a build exists!
            for built_obj in (
                ROOT / "local-data/port/build/linux/port/game/ai/ce_ai_pilot_l1.cpp.o",
                ROOT / "local-data/port/build/windows/port/game/ai/ce_ai_pilot_l1.cpp.o",
            ):
                if built_obj.is_file():
                    nm2 = subprocess.run([str(LINUX_NM), "-u", str(built_obj)], capture_output=True, text=True, check=True)
                    undef2 = {line.split()[-1].lstrip("_") for line in nm2.stdout.splitlines() if line.strip()}
                    bad2 = sorted(undef2 & FORBIDDEN_LIBM)
                    self.assertEqual(bad2, [], f"Unexpected libm imports in {built_obj}: {bad2}")

    def test_02_10000_tick_linux_vs_windows_bit_identical_and_l1_budget(self):
        self.assertTrue(LINUX_CLANGXX.is_file(), f"Pinned Linux clang++ missing at {LINUX_CLANGXX}")
        self.assertTrue(WIN_CLANGXX.is_file(), f"Pinned Windows clang++ missing at {WIN_CLANGXX}")
        wine = shutil.which("wine")
        self.assertIsNotNone(wine, "wine is required for Linux-vs-Windows determinism gate")

        with tempfile.TemporaryDirectory(prefix="ce_ai_det_") as td:
            td_path = Path(td)
            driver = td_path / "rollout.cpp"
            driver.write_text(ROLLOUT_DRIVER, encoding="utf-8")
            wine_env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
            wine_env["WINEDEBUG"] = "-all"
            wine_env["WINEPREFIX"] = str(td_path / "wineprefix")

            mode_hashes: dict[str, str] = {}
            mode_sizes: dict[str, int] = {}
            for label, extra in (
                ("default", []),
                ("trained_weights", ["-DCE_AI_PILOT_TRAINED_WEIGHTS"]),
            ):
                with self.subTest(mode=label):
                    linux_bin = td_path / f"rollout_linux_{label}"
                    win_bin = td_path / f"rollout_win_{label}.exe"

                    linux_cmd = [
                        str(LINUX_CLANGXX), "--target=i686-pc-linux-gnu",
                        *deps.sysroot_flags(),
                        "-std=c++17", "-mno-sse", "-mfpmath=387", "-O2",
                        *extra,
                        f"-I{PORT / 'game'}",
                        str(driver), str(PORT / "game/ai/ce_ai_tactical_l2.cpp"),
                        "-o", str(linux_bin),
                    ]
                    win_cmd = [
                        str(WIN_CLANGXX),
                        "-std=c++17", "-mno-sse", "-mfpmath=387", "-O2", "-static",
                        *extra,
                        f"-I{PORT / 'game'}",
                        str(driver), str(PORT / "game/ai/ce_ai_tactical_l2.cpp"),
                        "-o", str(win_bin),
                    ]
                    subprocess.run(linux_cmd, check=True)
                    subprocess.run(win_cmd, check=True)
                    mode_sizes[label] = linux_bin.stat().st_size

                    lin_res = subprocess.run([str(linux_bin)], capture_output=True, text=True, check=True)
                    win_res = subprocess.run([wine, str(win_bin)], env=wine_env, capture_output=True, text=True, check=True)

                    lin_kv = _parse_kv(lin_res.stdout)
                    win_kv = _parse_kv(win_res.stdout)
                    self.assertEqual(lin_kv["HASH_EPOCH1"], lin_kv["HASH_EPOCH77"], f"{label}: Linux epoch invariance failed")
                    self.assertEqual(win_kv["HASH_EPOCH1"], win_kv["HASH_EPOCH77"], f"{label}: Windows epoch invariance failed")
                    self.assertEqual(
                        lin_kv["HASH_EPOCH1"], win_kv["HASH_EPOCH1"],
                        f"{label}: 10,000-tick Linux vs Windows divergence! linux={lin_kv} win={win_kv}",
                    )
                    mode_hashes[label] = lin_kv["HASH_EPOCH1"]

                    p99_ns = int(lin_kv["P99_NS"])
                    self.assertLess(
                        p99_ns, 100_000,
                        f"{label}: Layer-1 p99 exceeded 0.1 ms/tick budget: {p99_ns / 1000.0:.2f} us/tick",
                    )

            self.assertNotEqual(
                mode_hashes["default"], mode_hashes["trained_weights"],
                "Default and -DCE_AI_PILOT_TRAINED_WEIGHTS rollouts must differ (proving MLP is gated off by default)",
            )
            self.assertGreater(
                mode_sizes["trained_weights"], mode_sizes["default"] + 100_000,
                f"Default binary should omit 107.5 KB weights: default={mode_sizes['default']} trained={mode_sizes['trained_weights']}",
            )

    def test_03_l3_host_only_isolation_and_graceful_fallback(self):
        l3_src = PORT / "game/ai/ce_ai_strategist_l3.cpp"
        text = l3_src.read_text(encoding="utf-8")
        # Strip comments before scanning for forbidden game headers/allocators/RNG.
        code = re.sub(r"//.*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))
        for bad in ('"common.h"', "MemoryManager.h", "MEM_MANAGER", "bea_game_new", "bea_crt_rand", "rand("):
            self.assertNotIn(bad, code, f"ce_ai_strategist_l3.cpp must never reference {bad}")

        with tempfile.TemporaryDirectory(prefix="ce_ai_l3_") as td:
            td_path = Path(td)
            obj = td_path / "l3.o"
            subprocess.run(
                [str(LINUX_CLANGXX), "--target=i686-pc-linux-gnu", *deps.sysroot_flags(),
                 "-std=c++17", "-O2", f"-I{PORT / 'game'}", "-c", str(l3_src), "-o", str(obj)],
                check=True,
            )
            nm = subprocess.run([str(LINUX_NM), "-u", str(obj)], capture_output=True, text=True, check=True)
            undef = {line.split()[-1] for line in nm.stdout.splitlines() if line.strip()}
            for sym in ("MEM_MANAGER", "bea_game_new", "bea_game_delete", "bea_crt_rand", "rand", "GAME", "WORLD"):
                self.assertNotIn(sym, undef, f"L3 object must not import {sym}")

            # Host ASan/UBSan functional test (absent weights + valid mock strategist.bin).
            driver = td_path / "l3_driver.cpp"
            driver.write_text(L3_DRIVER, encoding="utf-8")
            exe = td_path / "l3_driver"
            host_cxx = shutil.which("clang++") or shutil.which("g++")
            self.assertIsNotNone(host_cxx)
            subprocess.run(
                [host_cxx, "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-pthread",
                 f"-I{PORT / 'game'}", str(driver), str(l3_src), "-o", str(exe)],
                check=True,
            )
            empty_dir = td_path / "empty_user"
            empty_dir.mkdir()
            model_dir = td_path / "model_user"
            (model_dir / "ai-models").mkdir(parents=True)
            # Construct valid 96-byte strategist.bin with dirty control characters in label.
            import struct
            label = b"Command\x01\x07\nTest\x7fModel".ljust(64, b"\x00")
            blob = struct.pack("<8sII5hBBHH64s", b"CESTRAT1", 1, 0, 0, 10, 20, 5, 30, 140, 130, 0, 0, label)
            (model_dir / "ai-models/strategist.bin").write_bytes(blob)

            res = subprocess.run([str(exe), str(empty_dir), str(model_dir)], capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f"L3 driver failed: stdout={res.stdout} stderr={res.stderr}")
            self.assertIn("L3_OK", res.stdout)

    def test_04_mlp_feature_and_output_index_alignment(self):
        l1_h = (PORT / "game/ai/ce_ai_pilot_l1.h").read_text(encoding="utf-8")
        l1_cpp = (PORT / "game/ai/ce_ai_pilot_l1.cpp").read_text(encoding="utf-8")
        gen_py = (PORT / "tools/gen_ai_pilot_weights.py").read_text(encoding="utf-8")

        hdr_feats = {m.group(1): int(m.group(2)) for m in re.finditer(r"CE_AI_(FEAT_[A-Z0-9_]+)\s*=\s*(\d+)", l1_h) if m.group(1) != "FEAT_COUNT"}
        gen_feats = {m.group(1): int(m.group(2)) for m in re.finditer(r"^(FEAT_[A-Z0-9_]+)\s*=\s*(\d+)", gen_py, re.M)}
        self.assertEqual(len(hdr_feats), 32, f"Expected 32 CeAiMlpFeatIdx entries, got {len(hdr_feats)}")
        self.assertEqual(hdr_feats, gen_feats, "gen_ai_pilot_weights.py FEAT_* indices must match CeAiMlpFeatIdx 1:1")
        self.assertEqual(set(hdr_feats.values()), set(range(32)))

        hdr_outs = {m.group(1): int(m.group(2)) for m in re.finditer(r"CE_AI_(OUT_[A-Z0-9_]+)\s*=\s*(\d+)", l1_h) if m.group(1) != "OUT_COUNT"}
        gen_outs = {m.group(1): int(m.group(2)) for m in re.finditer(r"^(OUT_[A-Z0-9_]+)\s*=\s*(\d+)", gen_py, re.M) if m.group(1) != "OUT_DIM"}
        self.assertEqual(len(hdr_outs), 8, f"Expected 8 CeAiMlpOutIdx entries, got {len(hdr_outs)}")
        self.assertEqual(hdr_outs, gen_outs, "gen_ai_pilot_weights.py OUT_* indices must match CeAiMlpOutIdx 1:1")
        self.assertEqual(set(hdr_outs.values()), set(range(8)))

        # Verify ce_ai_pilot_l1.cpp consumes the named output indices rather than raw magic numbers.
        for name in ("CE_AI_OUT_STRAFE_TRIM", "CE_AI_OUT_FWD_TRIM", "CE_AI_OUT_JET_TURN_TRIM",
                     "CE_AI_OUT_JET_THRUST_TRIM", "CE_AI_OUT_JET_CRUISE_BIAS"):
            self.assertIn(f"mlpOutQ12[{name}]", l1_cpp)

        # Verify strategic prior indices [29..31] are wired into gen_ai_pilot_weights.py priors.
        for name in ("FEAT_PRIOR_AGGRESSION", "FEAT_PRIOR_AIR_BIAS", "FEAT_PRIOR_POSTURE"):
            self.assertIn(name, gen_py)


if __name__ == "__main__":
    unittest.main()
