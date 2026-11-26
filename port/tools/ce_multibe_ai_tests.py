#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fair-rules Battle Engine combat AI brain host ASan/UBSan + causal-mutant suite.

    python3 -B port/tools/ce_multibe_ai_tests.py [-v]

Compiles port/game/ce_multibe_ai.h directly with zero game-header dependencies and verifies:
  1. Velocity projectile lead (stationary, moving, beam, lead-time clamp, zero/NaN/Inf guards).
  2. Aim-gated sustained, charged, and lock-on firing cones + anti-overheat burst hysteresis.
  3. Matchup-aware weapon scoring (air, armor/BE, infantry, overheated, empty, out-of-range, torpedo)
     and anti-thrash hysteresis with immediate unusable-weapon override.
  4. Reactive damage tracking, Walker standoff range-keeping, dry-ground probes, and fair-rules
     Walker Dash state machine timing & cooldown.
  5. Jet boom-and-zoom flight control, BEA azimuth/pitch signs, breakoff loops, and terrain/water
     look-ahead emergency pull-up safety.
  6. Base-defense threat prioritization, finish-wounded bonus, and contested battle-line scoring.
  7. Low-HP repair-retreat hysteresis, energy-over-water dry-shore routing, and strict
     never-land-over-water morph invariant.
  8. O(1) angle wrapping on extreme finite/NaN/Inf values and 20 causal mutants all caught.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
SCRATCH = ROOT / "local-data/port/ce-multibe-ai-tests"

HOST_DRIVER = r"""
#include "ce_multibe_ai.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int gFail = 0;
static void Check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        gFail++;
    }
}

static bool Near(float a, float b, float eps = 1e-2f) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) <= eps;
}

int main() {
    const float nanv = std::numeric_limits<float>::quiet_NaN();
    const float infv = std::numeric_limits<float>::infinity();

    // ---- 0. O(1) angle wrapping on normal, extreme finite, and non-finite inputs ----
    {
        const float kPi = 3.14159265f;
        Check(Near(CeAiWrapAngle(0.5f), 0.5f, 1e-4f), "wrap angle identity in range");
        Check(Near(CeAiWrapAngle(3.0f * kPi), kPi, 1e-3f) || Near(CeAiWrapAngle(3.0f * kPi), -kPi, 1e-3f),
              "wrap angle reduces 3*pi into [-pi, pi]");
        float hugePos = CeAiWrapAngle(1e10f);
        float hugeNeg = CeAiWrapAngle(-1e30f);
        Check(CeAiIsFinite(hugePos) && std::fabs(hugePos) <= kPi + 1e-4f, "wrap angle O(1) on 1e10f");
        Check(CeAiIsFinite(hugeNeg) && std::fabs(hugeNeg) <= kPi + 1e-4f, "wrap angle O(1) on -1e30f");
        Check(CeAiWrapAngle(nanv) == 0.0f && CeAiWrapAngle(infv) == 0.0f, "wrap angle non-finite returns 0");
    }

    // ---- 1. Lead-point math ----
    {
        CeAiVec3 me = { 0.0f, 0.0f, 0.0f };
        CeAiVec3 tgt = { 0.0f, 100.0f, 0.0f };
        CeAiVec3 zeroVel = { 0.0f, 0.0f, 0.0f };
        CeAiVec3 p0 = CeMultiBeComputeLeadPoint(me, tgt, zeroVel, 100.0f);
        Check(Near(p0.x, 0.0f) && Near(p0.y, 100.0f) && Near(p0.z, 0.0f), "stationary target returns exact pos");

        // Target at (0, 100, 0) moving +20 m/s in X, projectile 100 m/s -> 2-iter intercept > 20m in X
        CeAiVec3 latVel = { 20.0f, 0.0f, 0.0f };
        CeAiVec3 pLat = CeMultiBeComputeLeadPoint(me, tgt, latVel, 100.0f);
        Check(pLat.x > 20.1f && pLat.x < 21.0f && Near(pLat.y, 100.0f), "lateral 2-iteration lead point accurate");

        // Hitscan beam (>=950 m/s) returns target pos directly without over-leading
        CeAiVec3 pBeam = CeMultiBeComputeLeadPoint(me, tgt, latVel, 1000.0f);
        Check(Near(pBeam.x, 0.0f) && Near(pBeam.y, 100.0f), "fast beam returns direct target pos");

        // Lead time clamped to <= 2.2s even for slow projectile at long range
        CeAiVec3 farTgt = { 0.0f, 400.0f, 0.0f };
        CeAiVec3 pClamp = CeMultiBeComputeLeadPoint(me, farTgt, latVel, 40.0f, 2.2f);
        Check(Near(pClamp.x, 44.0f, 0.1f), "lead time clamped to 2.2s (20 * 2.2 = 44m)");

        // Zero, negative, NaN speeds and NaN/Inf vectors fail safely with finite coords
        CeAiVec3 pZeroSpd = CeMultiBeComputeLeadPoint(me, tgt, latVel, 0.0f);
        Check(Near(pZeroSpd.x, 0.0f) && Near(pZeroSpd.y, 100.0f), "zero projSpeed returns tgtPos");
        CeAiVec3 pNegSpd = CeMultiBeComputeLeadPoint(me, tgt, latVel, -50.0f);
        Check(Near(pNegSpd.x, 0.0f), "negative projSpeed returns tgtPos");
        CeAiVec3 pNanSpd = CeMultiBeComputeLeadPoint(me, tgt, latVel, nanv);
        Check(CeAiIsFiniteVec(pNanSpd) && Near(pNanSpd.y, 100.0f), "NaN projSpeed returns tgtPos");
        CeAiVec3 nanVel = { nanv, 0.0f, 0.0f };
        CeAiVec3 pNanVel = CeMultiBeComputeLeadPoint(me, tgt, nanVel, 100.0f);
        Check(CeAiIsFiniteVec(pNanVel) && Near(pNanVel.x, 0.0f), "NaN tgtVel returns tgtPos");
        CeAiVec3 infMe = { infv, 0.0f, 0.0f };
        CeAiVec3 pInfMe = CeMultiBeComputeLeadPoint(infMe, tgt, latVel, 100.0f);
        Check(CeAiIsFiniteVec(pInfMe) && Near(pInfMe.y, 100.0f), "Inf myPos returns tgtPos");
    }

    // ---- 2. Aim-gated sustained, charged, lock-on firing & heat hysteresis ----
    {
        CeMultiBeFireGateInput f = {};
        f.hasHostile = true;
        f.weaponActive = true;
        f.readyToFire = true;
        f.readyToCharge = true;
        f.isEnergyWeapon = true;
        f.heatOrAmmoRatio = 0.25f;
        f.targetDist = 70.0f;
        f.minRange = 5.0f;
        f.maxRange = 180.0f;
        f.yawErrAbs = 0.08f;
        f.pitchErrAbs = 0.06f;
        f.maxYawCone = 0.22f;
        f.maxPitchCone = 0.25f;

        // Cleanly aimed & ready -> shoot immediately!
        CeMultiBeFireDecision d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_SHOOT && !d.nextHeatPause, "aimed ready weapon shoots");

        // Turned 90 deg away -> MUST NOT shoot!
        f.yawErrAbs = 1.57f;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_HOLD, "holds fire when turned 90 deg away");

        // Pitch way off -> MUST NOT shoot!
        f.yawErrAbs = 0.05f;
        f.pitchErrAbs = 0.60f;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_HOLD, "holds fire when pitch error exceeds cone");
        f.pitchErrAbs = 0.05f;

        // No hostile -> MUST NOT shoot!
        f.hasHostile = false;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_HOLD, "holds fire with no hostile");
        f.hasHostile = true;

        // Out of range (too far / too close) -> MUST NOT shoot!
        f.targetDist = 250.0f;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "holds fire beyond maxRange");
        f.targetDist = 2.0f;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "holds fire inside minRange");
        f.targetDist = 70.0f;

        // Emergency Jet pull-up suppresses fire
        f.emergencyPullUp = true;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "emergency pull-up suppresses fire");
        f.emergencyPullUp = false;

        // Overheated / empty ballistic -> hold
        f.isOverheated = true;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "overheated weapon holds fire");
        f.isOverheated = false;
        f.isEnergyWeapon = false;
        f.ammoCount = 0;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "empty ballistic weapon holds fire");
        f.ammoCount = 20;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_SHOOT, "loaded ballistic weapon shoots");

        // Anti-overheat hysteresis for energy weapons
        f.isEnergyWeapon = true;
        f.heatOrAmmoRatio = 0.90f;
        f.heatCoolingPause = false;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_HOLD && d.nextHeatPause, "enters anti-overheat pause at 90% heat");
        f.heatCoolingPause = true;
        f.heatOrAmmoRatio = 0.72f;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_HOLD && d.nextHeatPause, "stays paused at 72% heat");
        f.heatOrAmmoRatio = 0.58f;
        d = CeMultiBeEvaluateFireGate(f);
        Check(d.action == CE_AI_FIRE_SHOOT && !d.nextHeatPause, "resumes firing once cooled below 62%");

        // Charge weapon: charges first, fires when fully charged
        f.heatOrAmmoRatio = 0.2f;
        f.heatCoolingPause = false;
        f.canCharge = true;
        f.fullyCharged = false;
        f.chargeRatio = 0.30f;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_CHARGE, "charge weapon charges up");
        f.fullyCharged = true;
        f.chargeRatio = 1.0f;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_SHOOT, "charge weapon fires when fully charged");
        f.canCharge = false;

        // Lock-on weapon: holds during lock acquisition at 80m, fires with wide cone when locked
        f.isLockWeapon = true;
        f.activeLocks = 1;
        f.locksFinished = false;
        f.targetDist = 80.0f;
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_HOLD, "waits briefly while missile lock completes");
        f.locksFinished = true;
        f.yawErrAbs = 0.32f; // outside 0.22 normal cone, inside 1.65x locked cone
        Check(CeMultiBeEvaluateFireGate(f).action == CE_AI_FIRE_SHOOT, "fires locked missile within wider cone");
    }

    // ---- 3. Matchup-aware weapon scoring & anti-thrash selection ----
    {
        CeMultiBeWeaponInfo vulcan = {};
        vulcan.slotIndex = 0; vulcan.isActive = true; vulcan.isEnergy = true;
        vulcan.minRange = 0.0f; vulcan.maxRange = 160.0f; vulcan.launchSpeed = 160.0f;
        vulcan.damage = 2.0f; vulcan.reloadTime = 0.12f; vulcan.burstSize = 3;
        vulcan.targetTypeOk = true;

        CeMultiBeWeaponInfo aaMissile = {};
        aaMissile.slotIndex = 1; aaMissile.isActive = true; aaMissile.isEnergy = false; aaMissile.ammoCount = 12;
        aaMissile.minRange = 15.0f; aaMissile.maxRange = 240.0f; aaMissile.launchSpeed = 130.0f;
        aaMissile.damage = 14.0f; aaMissile.power = 0.35f; aaMissile.reloadTime = 0.8f;
        aaMissile.isHoming = true; aaMissile.isMissile = true; aaMissile.isExplosive = true;
        aaMissile.canLockTarget = true; aaMissile.targetTypeOk = true;

        CeMultiBeWeaponInfo grenade = {};
        grenade.slotIndex = 2; grenade.isActive = true; grenade.isEnergy = false; grenade.ammoCount = 20;
        grenade.minRange = 10.0f; grenade.maxRange = 120.0f; grenade.launchSpeed = 45.0f;
        grenade.damage = 18.0f; grenade.power = 0.45f; grenade.reloadTime = 0.7f;
        grenade.isExplosive = true; grenade.isBallistic = true; grenade.targetTypeOk = true;

        CeMultiBeWeaponInfo torpedo = {};
        torpedo.slotIndex = 3; torpedo.isActive = true; torpedo.isEnergy = false; torpedo.ammoCount = 8;
        torpedo.minRange = 10.0f; torpedo.maxRange = 180.0f; torpedo.launchSpeed = 60.0f;
        torpedo.damage = 22.0f; torpedo.isTorpedo = true; torpedo.targetTypeOk = true;

        // Airborne target: homing AA missile beats gravity grenade!
        CeMultiBeWeaponTargetCtx airCtx = { CE_AI_TGT_AIRCRAFT, 100.0f, 25.0f, false, false };
        Check(CeMultiBeScoreWeapon(aaMissile, airCtx) > CeMultiBeScoreWeapon(grenade, airCtx) + 50.0f,
              "AA homing missile strongly outscores gravity grenade vs aircraft");
        Check(CeMultiBeScoreWeapon(torpedo, airCtx) <= -900.0f, "torpedo unusable against non-water target");

        // Battle Engine / heavy armor: missile/explosive beats plain low-dmg vulcan
        CeMultiBeWeaponTargetCtx beCtx = { CE_AI_TGT_BATTLE_ENGINE, 80.0f, 0.0f, false, false };
        Check(CeMultiBeScoreWeapon(aaMissile, beCtx) > CeMultiBeScoreWeapon(vulcan, beCtx),
              "heavy homing missile outscores light vulcan vs Battle Engine");

        // Infantry: rapid energy vulcan beats slow sniper charge weapon
        CeMultiBeWeaponInfo sniper = {};
        sniper.slotIndex = 4; sniper.isActive = true; sniper.isEnergy = true;
        sniper.minRange = 0.0f; sniper.maxRange = 220.0f; sniper.launchSpeed = 240.0f;
        sniper.damage = 10.0f; sniper.reloadTime = 1.6f; sniper.canCharge = true; sniper.targetTypeOk = true;
        CeMultiBeWeaponTargetCtx infCtx = { CE_AI_TGT_INFANTRY, 55.0f, 0.0f, false, false };
        Check(CeMultiBeScoreWeapon(vulcan, infCtx) > CeMultiBeScoreWeapon(sniper, infCtx),
              "rapid energy cannon beats slow sniper charge vs infantry");

        // Overheated & out-of-ammo usability checks
        CeMultiBeWeaponInfo emptyMsl = aaMissile;
        emptyMsl.ammoCount = 0;
        Check(CeMultiBeScoreWeapon(emptyMsl, airCtx) <= -900.0f, "empty ballistic weapon rejected");
        CeMultiBeWeaponInfo hotVulcan = vulcan;
        hotVulcan.isOverheated = true;
        Check(CeMultiBeScoreWeapon(hotVulcan, infCtx) < -100.0f, "overheated weapon heavily penalized");

        // Anti-thrash hysteresis vs emergency switch
        CeMultiBeWeaponInfo loadout[3] = { vulcan, aaMissile, grenade };
        // Voluntary switch blocked during cooldown (< 50 ticks)
        Check(CeMultiBePickWeapon(loadout, 3, 0, airCtx, 15, 50, 14.0f) == 0, "cooldown prevents thrashing");
        // Voluntary switch allowed after cooldown
        Check(CeMultiBePickWeapon(loadout, 3, 0, airCtx, 60, 50, 14.0f) == 1, "switches to AA missile after cooldown");
        // Emergency switch bypasses cooldown when current weapon is overheated!
        loadout[0].isOverheated = true;
        Check(CeMultiBePickWeapon(loadout, 3, 0, airCtx, 5, 50, 14.0f) == 1,
              "overheated current weapon switches immediately bypassing cooldown");
        // Out-of-range current weapon switches immediately bypassing cooldown!
        loadout[0].isOverheated = false;
        CeMultiBeWeaponTargetCtx longCtx = { CE_AI_TGT_BATTLE_ENGINE, 210.0f, 0.0f, false, false };
        Check(CeMultiBePickWeapon(loadout, 3, 2, longCtx, 5, 50, 14.0f) == 1,
              "out-of-range weapon switches immediately to in-range weapon");
    }

    // ---- 4. Reactive damage, Walker standoff, water safety & Walker Dash ----
    {
        CeMultiBeDamageState dmg;
        dmg.Reset(100.0f);
        dmg.Update(92.0f, 100.0f, 10.0f, 10.05f); // 8% instant hit -> heavy hit!
        Check(dmg.underFireTicks > 0 && dmg.heavyHitPending, "damage tracker flags underFire & heavyHit");

        CeMultiBeWalkerMoveInput wm = {};
        wm.hasHostile = true;
        wm.targetDist = 110.0f;
        wm.strafeDir = 1;
        wm.strafeTicks = 20;
        wm.dryFwd = wm.dryBack = wm.dryLeft = wm.dryRight = wm.dryDashLeft = wm.dryDashRight = true;

        // Far target (>85m): advances forward (negative input in BEA WalkerPart::Forward!)
        CeMultiBeWalkerMoveCmd wc = CeMultiBeComputeWalkerMove(wm);
        Check(wc.forwardInput < -0.5f && wc.backwardInput == 0.0f, "walker advances forward (negative vy) when >85m");

        // Crowded (<22m): backpedals (positive vy) & circle-strafes
        wm.targetDist = 16.0f;
        wc = CeMultiBeComputeWalkerMove(wm);
        Check(wc.backwardInput > 0.5f && std::fabs(wc.strafeInput) > 0.5f, "walker backpedals & strafes when <22m");

        // Under heavy fire at 55m: reverses strafe & requests dash!
        wm.targetDist = 55.0f;
        wm.underFire = true;
        wm.heavyHit = true;
        wm.strafeDir = 1;
        wm.strafeTicks = 20;
        wc = CeMultiBeComputeWalkerMove(wm);
        Check(wc.reversedStrafe && wc.nextStrafeDir == -1, "reverses strafe direction under fire");
        Check(wc.wantDash && wc.dashDir == -1, "requests dash toward dry side on heavy hit");

        // Water safety: if left is wet, NEVER strafes or dashes left!
        wm.dryLeft = false;
        wm.dryDashLeft = false;
        wm.dryRight = true;
        wm.dryDashRight = true;
        wm.strafeDir = 1;
        wm.strafeTicks = 20;
        wc = CeMultiBeComputeWalkerMove(wm);
        Check(wc.strafeInput > 0.0f, "walker refuses to strafe into water");
        Check(wc.wantDash && wc.dashDir == 1, "walker dashes only toward dry side");

        // If BOTH dash sides are wet, refuses dash completely
        wm.dryDashRight = false;
        wc = CeMultiBeComputeWalkerMove(wm);
        Check(!wc.wantDash, "walker suppresses dash when both 16m lateral probes are wet");

        // On repair pad while retreating: holds still
        wm.isRetreating = true;
        wm.onRepairPad = true;
        wc = CeMultiBeComputeWalkerMove(wm);
        Check(wc.forwardInput == 0.0f && wc.backwardInput == 0.0f && wc.strafeInput == 0.0f,
              "walker holds still on repair pad");

        // Fair-rules Walker Dash timing state machine (opposite flick at t0, quiet at 0.05/0.10, fire at 0.15s)
        CeMultiBeWalkerDashState ds;
        ds.Reset();
        // Arm dash LEFT (-1) at t=1.00 -> must emit opposite hard RIGHT (+0.96)
        CeMultiBeWalkerDashOutput d0 = CeMultiBeStepWalkerDash(ds, true, -1, true, 0, true, 1.00f);
        Check(d0.overrideStrafe && d0.strafeValue > 0.90f && !d0.dashTriggered && ds.phase == CE_AI_DASH_ARMED,
              "dash left arms with opposite hard right push >0.9");
        // Quiet ticks at 1.05s and 1.10s
        CeMultiBeWalkerDashOutput d1 = CeMultiBeStepWalkerDash(ds, false, -1, true, 0, true, 1.05f);
        Check(d1.overrideStrafe && d1.strafeValue == 0.0f && !d1.dashTriggered, "holds quiet at dt=0.05s");
        CeMultiBeWalkerDashOutput d2 = CeMultiBeStepWalkerDash(ds, false, -1, true, 0, true, 1.10f);
        Check(d2.overrideStrafe && d2.strafeValue == 0.0f && !d2.dashTriggered, "holds quiet at dt=0.10s");
        // Fire tick at 1.15s (dead center of 0.10..0.20s retail window!) -> emits hard LEFT (-0.96)
        CeMultiBeWalkerDashOutput d3 = CeMultiBeStepWalkerDash(ds, false, -1, true, 0, true, 1.15f);
        Check(d3.overrideStrafe && d3.strafeValue < -0.90f && d3.dashTriggered && ds.cooldownTicks > 0,
              "completes dash left at dt=0.15s and starts cooldown");
        // Cooldown blocks immediate re-arm
        CeMultiBeWalkerDashOutput d4 = CeMultiBeStepWalkerDash(ds, true, -1, true, 0, true, 1.25f);
        Check(!d4.overrideStrafe && ds.phase == CE_AI_DASH_IDLE, "dash cooldown blocks immediate re-trigger");
        // Non-zero engineDashCount blocks arming
        ds.Reset();
        CeMultiBeWalkerDashOutput d5 = CeMultiBeStepWalkerDash(ds, true, -1, true, 8, true, 2.00f);
        Check(!d5.overrideStrafe, "active engine dash count blocks arming");
    }

    // ---- 5. Jet boom-and-zoom & terrain/water pull-up safety ----
    {
        // BEA azimuth sign: target at (+50, +50) is ahead-RIGHT -> desiredYaw = -pi/4 -> turnInput > 0 (turns RIGHT)
        CeMultiBeJetFlightInput ji = {};
        ji.curYaw = 0.0f;
        ji.curPitch = 0.0f;
        ji.posZ = -40.0f;
        ji.groundZ = -10.0f;
        ji.aheadGroundZ = -10.0f;
        ji.waterZ = -5.0f;
        ji.toGoal = { 50.0f, 50.0f, 10.0f };
        ji.hasHostile = true;
        ji.desiredCruiseHeight = 25.0f;
        ji.prevPhase = CE_AI_JET_ATTACK_RUN;
        ji.breakTurnDir = 1;

        CeMultiBeJetFlightCmd jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.turnInput > 0.3f, "jet turns RIGHT (+turnInput) toward +X target using BEA -atan2(dx,dy)");
        Check(jc.inAttackRun && !jc.emergencyPullUp, "jet stays in attack run at 70m safe altitude");

        // Close pass (<38m) triggers clean breakoff climb-turn!
        ji.toGoal = { 0.0f, 28.0f, 10.0f };
        jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.nextPhase == CE_AI_JET_BREAKOFF && jc.startedBreakoff && !jc.inAttackRun,
              "jet breaks off cleanly below 38m");

        // Heavy AA fire also triggers breakoff!
        ji.toGoal = { 0.0f, 95.0f, 10.0f };
        ji.underHeavyFire = true;
        jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.nextPhase == CE_AI_JET_BREAKOFF && jc.startedBreakoff, "jet breaks off under heavy AA fire");
        ji.underHeavyFire = false;

        // Terrain/water pull-up safety: low over water OR high cliff ahead -> pitchInput > 0 (pulls nose UP!)
        ji.posZ = -12.0f; // only 7m above waterZ=-5 and 2m above groundZ=-10!
        ji.curPitch = 0.15f; // currently pitched slightly DOWN
        ji.toGoal = { 0.0f, 100.0f, 5.0f };
        jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.emergencyPullUp && jc.pitchInput > 0.5f && jc.thrustVy <= -0.80f,
              "jet emergency pull-up commands strong positive pitchInput (nose UP) & full thrust");

        // Ahead mountain cliff safety: high above current ground (-10) but hillside ahead rises to -35!
        ji.posZ = -42.0f;
        ji.groundZ = -10.0f;
        ji.aheadGroundZ = -35.0f; // only 7m clearance over ahead ridge!
        ji.curPitch = 0.0f;
        jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.emergencyPullUp && jc.pitchInput > 0.4f, "jet pulls UP early for rising terrain ahead");

        // Distant takeoff climb: low altitude 180m away from target must pull UP while still heading TOWARD target (NOT breakoff!).
        ji.posZ = -6.0f;
        ji.groundZ = 0.0f;
        ji.aheadGroundZ = 0.0f;
        ji.waterZ = 2.0f;
        ji.curYaw = 0.0f;
        ji.curPitch = 0.0f;
        ji.toGoal.x = 0.0f;
        ji.toGoal.y = 180.0f;
        ji.toGoal.z = 5.0f;
        ji.prevPhase = CE_AI_JET_CRUISE;
        jc = CeMultiBeComputeJetFlight(ji);
        Check(jc.emergencyPullUp && jc.pitchInput > 0.4f && jc.nextPhase != CE_AI_JET_BREAKOFF && fabsf(jc.turnInput) < 0.1f,
              "distant takeoff climbs straight toward target without flying backwards in breakoff");
    }

    // ---- 6. Tactical threat prioritization & battle-line nodes ----
    {
        CeMultiBeTargetCandidate beTgt = {};
        beTgt.unitClass = CE_AI_TGT_BATTLE_ENGINE;
        beTgt.distToMe = 90.0f;
        beTgt.distToNearestFriendlyBase = 200.0f;
        beTgt.targetLifeRatio = 1.0f;

        CeMultiBeTargetCandidate infTgt = {};
        infTgt.unitClass = CE_AI_TGT_INFANTRY;
        infTgt.distToMe = 90.0f;
        infTgt.distToNearestFriendlyBase = 200.0f;
        infTgt.targetLifeRatio = 1.0f;

        Check(CeMultiBeScoreTarget(beTgt) > CeMultiBeScoreTarget(infTgt) + 60.0f,
              "opposing Battle Engine strongly outranks infantry at equal distance");

        // Base-defense urgency: distant attacker threatening damaged friendly factory outranks closer stray unit!
        CeMultiBeTargetCandidate baseAttacker = {};
        baseAttacker.unitClass = CE_AI_TGT_VEHICLE;
        baseAttacker.distToMe = 180.0f;
        baseAttacker.distToNearestFriendlyBase = 25.0f;
        baseAttacker.friendlyBaseUnderAttack = true;
        baseAttacker.targetLifeRatio = 1.0f;

        CeMultiBeTargetCandidate strayNear = {};
        strayNear.unitClass = CE_AI_TGT_VEHICLE;
        strayNear.distToMe = 80.0f;
        strayNear.distToNearestFriendlyBase = 220.0f;
        strayNear.targetLifeRatio = 1.0f;

        Check(CeMultiBeScoreTarget(baseAttacker) > CeMultiBeScoreTarget(strayNear) + 50.0f,
              "attacker damaging friendly base outranks closer non-threatening unit");

        // In-range engagement envelope: active vehicle right in front (65m) outranks distant out-of-range BE (220m)
        CeMultiBeTargetCandidate farBe = beTgt;
        farBe.distToMe = 220.0f;
        farBe.isCurrentTarget = true;
        CeMultiBeTargetCandidate nearVeh = strayNear;
        nearVeh.distToMe = 65.0f;
        Check(CeMultiBeScoreTarget(nearVeh) > CeMultiBeScoreTarget(farBe) + 20.0f,
              "in-range enemy unit outranks distant out-of-range BE");

        // Wounded target priority
        CeMultiBeTargetCandidate wounded = strayNear;
        wounded.targetLifeRatio = 0.15f;
        Check(CeMultiBeScoreTarget(wounded) > CeMultiBeScoreTarget(strayNear) + 10.0f,
              "wounded target gets finish-off bonus");

        // Contested INFLUENCEMAP node beats friendly rear node
        float contested = CeMultiBeScoreBattleLineNode(false, 0.05f, 4.0f, 4.0f, 120.0f);
        float friendlyRear = CeMultiBeScoreBattleLineNode(false, 0.95f, 8.0f, 0.0f, 40.0f);
        Check(contested > friendlyRear + 100.0f, "contested battle-line node strongly beats friendly rear node");
    }

    // ---- 7. Survival repair-retreat hysteresis, energy safety & smart morph ----
    {
        CeMultiBeSurvivalInput si = {};
        si.lifeRatio = 0.30f;
        si.energyRatio = 0.80f;
        si.isJet = false;
        si.groundIsDry = true;
        si.hasRepairPad = true;
        si.currentlyRetreating = false;

        CeMultiBeSurvivalDecision sd = CeMultiBeEvaluateSurvival(si);
        Check(sd.retreating && !sd.defensiveStandoff, "enters retreat below 35% HP when repair pad exists");

        // Hysteresis: stays retreating at 50% HP, exits once >= 65% HP
        si.currentlyRetreating = true;
        si.lifeRatio = 0.50f;
        Check(CeMultiBeEvaluateSurvival(si).retreating, "stays retreating at 50% HP (hysteresis)");
        si.lifeRatio = 0.68f;
        Check(!CeMultiBeEvaluateSurvival(si).retreating, "exits retreat once healed past 65% HP");

        // No sanctuary -> defensive standoff instead of retreating
        si.hasRepairPad = false;
        si.hasFriendlyBase = false;
        si.currentlyRetreating = false;
        si.lifeRatio = 0.30f;
        sd = CeMultiBeEvaluateSurvival(si);
        Check(!sd.retreating && sd.defensiveStandoff, "uses defensive standoff when no repair/base sanctuary exists");

        // Energy over water -> seek dry shore, NEVER land over water!
        si.isJet = true;
        si.energyRatio = 0.28f;
        si.groundIsDry = false;
        sd = CeMultiBeEvaluateSurvival(si);
        Check(sd.seekDryShore && !sd.wantSafeEnergyLand, "seeks dry shore when low energy over water");

        CeMultiBeSmartMorphInput sm = {};
        sm.isJet = true;
        sm.flightAllowed = true;
        sm.groundIsDry = false;
        sm.aheadGroundIsDry = true;
        sm.altitudeAboveGround = 4.0f;
        sm.energyRatio = 0.05f;
        sm.ticksSinceMorph = 200;
        Check(!CeMultiBeShouldMorphSmart(sm), "NEVER morphs Jet->Walker over water even at 5% energy");

        // High-altitude Jet->Walker morphing MUST be refused ("flying spider" prevention)!
        sm.groundIsDry = true;
        sm.aheadGroundIsDry = true;
        sm.energyRatio = 0.25f;
        sm.altitudeAboveGround = 30.0f;
        Check(!CeMultiBeShouldMorphSmart(sm), "refuses Jet->Walker low-energy morph at 30m AGL");
        sm.energyRatio = 0.80f;
        sm.hasHostile = true;
        sm.targetDist = 35.0f;
        sm.targetAltitude = 0.0f;
        sm.ticksSinceMorph = 200;
        Check(!CeMultiBeShouldMorphSmart(sm), "refuses Jet->Walker combat morph at 30m AGL");

        // Wet runway ahead refuses normal low-altitude landing morph
        sm.altitudeAboveGround = 4.5f;
        sm.aheadGroundIsDry = false;
        sm.energyRatio = 0.25f;
        Check(!CeMultiBeShouldMorphSmart(sm), "refuses Jet->Walker when forward landing runway is over water");

        // Low altitude over dry land + dry runway ahead -> lands cleanly!
        sm.aheadGroundIsDry = true;
        sm.energyRatio = 0.30f;
        Check(CeMultiBeShouldMorphSmart(sm), "lands cleanly over dry ground below 32% energy at <= 6m AGL");
        sm.energyRatio = 0.80f;
        Check(CeMultiBeShouldMorphSmart(sm), "lands cleanly for close ground combat at <= 6m AGL");
        sm.ticksSinceMorph = 90;
        Check(!CeMultiBeShouldMorphSmart(sm), "enforces 110-tick cooldown before combat Jet->Walker drop");

        // Critical energy (<10%) fallback allows slightly higher dry landing (<= 14m)
        sm.energyRatio = 0.08f;
        sm.altitudeAboveGround = 12.0f;
        Check(CeMultiBeShouldMorphSmart(sm), "allows critical low-energy dry landing at 12m AGL");

        // Base-defense scramble takeoff (requires grounded or low AGL)
        sm.isJet = false;
        sm.isOnGround = true;
        sm.altitudeAboveGround = 0.0f;
        sm.energyRatio = 0.60f;
        sm.baseDefenseUrgent = true;
        sm.targetDist = 100.0f;
        sm.ticksSinceMorph = 60;
        Check(CeMultiBeShouldMorphSmart(sm), "scrambles to Jet promptly when friendly base is under attack");

        // Airborne Walker MUST NOT morph back to Jet mid-fall unless rescuing from water!
        sm.isOnGround = false;
        sm.altitudeAboveGround = 15.0f;
        sm.nearWaterHazard = false;
        Check(!CeMultiBeShouldMorphSmart(sm), "refuses mid-air Walker->Jet morph when falling over dry land");
        sm.nearWaterHazard = true;
        Check(CeMultiBeShouldMorphSmart(sm), "allows emergency mid-air Walker->Jet water rescue");

        // Periodic sortie must not thrash during close-in Walker duel (< 60m)
        sm.isOnGround = true;
        sm.altitudeAboveGround = 0.0f;
        sm.nearWaterHazard = false;
        sm.baseDefenseUrgent = false;
        sm.energyRatio = 0.85f;
        sm.hasHostile = true;
        sm.targetDist = 35.0f;
        sm.targetAltitude = 0.0f;
        sm.ticksSinceMorph = 200;
        sm.slotIndex = 0;
        sm.simTicks = 240;
        Check(!CeMultiBeShouldMorphSmart(sm), "does not fire periodic air sortie during close-in 35m duel");
        sm.targetDist = 70.0f;
        Check(CeMultiBeShouldMorphSmart(sm), "fires periodic air sortie at standoff 70m range");

        // ---- 8. Jet Landing Approach & Post-Takeoff Smooth Climb ----
        Check(CeMultiBeWantsJetLanding(true, true, true, true, 0.28f, false, false, false, -1.0f, 0.0f, 50),
              "wants jet landing when energy < 34% over dry runway");
        Check(!CeMultiBeWantsJetLanding(true, true, true, false, 0.28f, false, false, false, -1.0f, 0.0f, 50),
              "refuses jet landing approach when forward runway is wet");
        Check(CeMultiBeWantsJetLanding(true, true, true, true, 0.80f, false, false, true, 40.0f, 0.0f, 120),
              "wants jet landing for close ground combat after cooldown");

        CeMultiBeJetFlightInput lj = {};
        lj.curYaw = 0.0f;
        lj.curPitch = 0.0f;
        lj.posZ = -28.0f;      // 18m above ground at z=-10
        lj.groundZ = -10.0f;
        lj.aheadGroundZ = -10.0f;
        lj.waterZ = 0.0f;
        lj.toGoal = { 0.0f, 40.0f, 18.0f };
        lj.hasHostile = true;
        lj.wantLanding = true;
        lj.groundIsDry = true;
        lj.aheadGroundIsDry = true;
        CeMultiBeJetFlightCmd lcmd = CeMultiBeComputeJetFlight(lj);
        Check(lcmd.nextPhase == CE_AI_JET_LANDING && lcmd.inLanding && !lcmd.emergencyPullUp,
              "enters CE_AI_JET_LANDING descent phase over dry runway");
        Check(lcmd.pitchInput < -0.05f && lcmd.thrustVy > 0.25f,
              "landing approach pitches nose down smoothly and cuts throttle");

        // Low flare at 5m AGL stays in landing (does NOT trigger 10.5m emergency pull-up!)
        lj.posZ = -15.0f;
        lcmd = CeMultiBeComputeJetFlight(lj);
        Check(lcmd.nextPhase == CE_AI_JET_LANDING && lcmd.inLanding && !lcmd.emergencyPullUp,
              "landing approach flares cleanly at 5m AGL without emergency pull-up");

        // Water ahead immediately aborts landing and pulls up!
        lj.aheadGroundIsDry = false;
        lcmd = CeMultiBeComputeJetFlight(lj);
        Check(!lcmd.inLanding && lcmd.emergencyPullUp && lcmd.pitchInput > 0.3f,
              "wet runway ahead aborts landing approach and triggers emergency pull-up");

        // Post-takeoff climb at 6m AGL climbs gently without afterburner emergency pull-up!
        CeMultiBeJetFlightInput tj = {};
        tj.curYaw = 0.0f;
        tj.curPitch = 0.0f;
        tj.posZ = -16.0f;      // 6m AGL
        tj.groundZ = -10.0f;
        tj.aheadGroundZ = -10.0f;
        tj.waterZ = 0.0f;
        tj.toGoal = { 0.0f, 120.0f, 0.0f };
        tj.hasHostile = true;
        tj.groundIsDry = true;
        tj.aheadGroundIsDry = true;
        tj.postTakeoffClimb = true;
        CeMultiBeJetFlightCmd tcmd = CeMultiBeComputeJetFlight(tj);
        Check(!tcmd.emergencyPullUp && tcmd.pitchInput > 0.10f && tcmd.pitchInput < 0.65f && tcmd.thrustVy > -0.60f,
              "post-takeoff climb rises smoothly without violent afterburner pull-up");
    }

    if (!gFail)
        std::puts("multibe-ai-host-ok");
    return gFail != 0;
}
"""


class MultiBeAiBrainTests(unittest.TestCase):
    def compile_and_run(self, ai_header_text, tag, sanitize=True):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        safe_tag = re.sub(r"[^A-Za-z0-9_.-]+", "-", tag)
        base = Path(tempfile.mkdtemp(prefix=f"ai-{safe_tag}-", dir=SCRATCH))
        try:
            (base / "ce_multibe_ai.h").write_text(ai_header_text, encoding="utf-8")
            driver = base / "driver.cpp"
            driver.write_text(HOST_DRIVER, encoding="utf-8")
            exe = base / "driver"
            cmd = [
                shutil.which("g++") or "c++",
                "-std=gnu++14",
                "-Wall",
                "-Werror",
                "-I",
                str(base),
                str(driver),
                "-o",
                str(exe),
            ]
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

    def test_spdx_and_zero_env_vars(self):
        text = (PORT / "game/ce_multibe_ai.h").read_text(encoding="utf-8")
        self.assertIn("SPDX-License-Identifier: GPL-3.0-only", text[:200])
        self.assertEqual(set(re.findall(r"BEA_[A-Z0-9_]+", text)), set())

    def test_ai_brain_passes_asan_ubsan(self):
        ai_src = (PORT / "game/ce_multibe_ai.h").read_text(encoding="utf-8")
        rc, out = self.compile_and_run(ai_src, "clean", sanitize=True)
        self.assertEqual(rc, 0, out)
        self.assertEqual(out.strip(), "multibe-ai-host-ok")

    def test_causal_mutants_all_caught(self):
        ai_src = (PORT / "game/ce_multibe_ai.h").read_text(encoding="utf-8")
        mutants = {
            "01_beam_overleads": (
                "if (projSpeed <= 1.0f || projSpeed >= 950.0f)",
                "if (projSpeed <= 1.0f)",
            ),
            "02_lead_clamp_dropped": (
                "const float t = CeAiClamp(dist / projSpeed, 0.0f, maxT);",
                "const float t = dist / projSpeed;",
            ),
            "03_lead_zero_speed_div": (
                "if (projSpeed <= 1.0f || projSpeed >= 950.0f)\n\t\treturn tgtPos;",
                "if (projSpeed < -1000.0f)\n\t\treturn tgtPos;",
            ),
            "04_aim_cone_disabled": (
                "const bool aimed = (in.yawErrAbs <= effYawCone) && (in.pitchErrAbs <= effPitchCone);",
                "const bool aimed = true;",
            ),
            "05_fires_without_hostile": (
                "if (!in.hasHostile || !in.weaponActive || in.isOverheated || in.emergencyPullUp)",
                "if (!in.weaponActive || in.isOverheated || in.emergencyPullUp)",
            ),
            "06_heat_pause_missing": (
                "else if (in.heatOrAmmoRatio >= 0.88f && !(in.canCharge && in.chargeRatio >= 0.50f))\n\t\t\tout.nextHeatPause = true;",
                "else if (false)\n\t\t\tout.nextHeatPause = true;",
            ),
            "07_empty_ammo_scored": (
                "if (!w.isEnergy && w.ammoCount <= 0)\n\t\treturn -1000.0f;",
                "if (false)\n\t\treturn -1000.0f;",
            ),
            "08_torpedo_on_land_allowed": (
                "if (w.isTorpedo && !ctx.targetInWater)\n\t\treturn -1000.0f;",
                "if (false)\n\t\treturn -1000.0f;",
            ),
            "09_weapon_cooldown_bypassed": (
                "if (ticksSinceSwitch >= minSwitchCooldown && bestScore > curScore + hysteresisMargin)",
                "if (bestScore > curScore + hysteresisMargin)",
            ),
            "10_walker_forward_sign_inverted": (
                "if (in.dryFwd)\n\t\t\tcmd.forwardInput = -0.80f;",
                "if (in.dryFwd)\n\t\t\tcmd.forwardInput = 0.80f;",
            ),
            "11_walker_strafes_into_water": (
                "bool chosenDry = (cmd.nextStrafeDir < 0) ? in.dryLeft : in.dryRight;",
                "bool chosenDry = true;",
            ),
            "12_dash_arms_same_dir_instead_of_opposite": (
                "out.strafeValue = (dir < 0) ? 0.96f : -0.96f;",
                "out.strafeValue = (dir < 0) ? -0.96f : 0.96f;",
            ),
            "13_jet_azimuth_sign_inverted": (
                "return -atan2f(dx, dy);",
                "return atan2f(dx, dy);",
            ),
            "14_jet_pullup_dives_into_ground": (
                "desiredPitch = -0.44f;\t// strong nose UP (-Z climb!)",
                "desiredPitch = 0.44f;\t// inverted!",
            ),
            "15_base_defense_boost_removed": (
                "if (c.friendlyBaseUnderAttack)\n\t\t\tscore += 95.0f;",
                "if (c.friendlyBaseUnderAttack)\n\t\t\tscore += 0.0f;",
            ),
            "16_retreat_hysteresis_dropped": (
                "if (in.currentlyRetreating)\n\t\t\tout.retreating = (life < 0.65f);",
                "if (in.currentlyRetreating)\n\t\t\tout.retreating = (life < 0.35f);",
            ),
            "17_jet_lands_over_water": (
                "if (!in.groundIsDry)\n\t\t\treturn false;",
                "if (false)\n\t\t\treturn false;",
            ),
            "18_jet_takeoff_turns_away_from_distant_target": (
                "if (horizDist < 38.0f || in.underHeavyFire || (in.posZ > floorDangerZ && horizDist < 52.0f))",
                "if (horizDist < 38.0f || in.underHeavyFire || in.posZ > floorDangerZ)",
            ),
            "19_in_range_engagement_bonus_removed": (
                "if (c.distToMe <= 105.0f)\n\t\tscore += 55.0f;",
                "if (c.distToMe <= 105.0f)\n\t\tscore += 0.0f;",
            ),
            "20_angle_wrap_returns_unwrapped": (
                "const float r = remainderf(a, 6.283185307179586f);",
                "const float r = a;",
            ),
            "21_high_altitude_jet_to_walker_allowed": (
                "if (alt > 8.0f || !in.aheadGroundIsDry)\n\t\t\treturn false;",
                "if (!in.aheadGroundIsDry)\n\t\t\treturn false;",
            ),
            "22_wet_runway_jet_to_walker_allowed": (
                "if (alt > 8.0f || !in.aheadGroundIsDry)\n\t\t\treturn false;",
                "if (alt > 8.0f)\n\t\t\treturn false;",
            ),
            "23_midair_walker_to_jet_allowed": (
                "if (!in.isOnGround && alt > 3.5f)\n\t\t\treturn false;",
                "if (false)\n\t\t\treturn false;",
            ),
            "24_close_duel_sortie_thrash_allowed": (
                "if (in.hasHostile && in.targetDist > 60.0f && ((in.simTicks + in.slotIndex * 65) % 240) < 10 && in.energyRatio > 0.65f)",
                "if (in.hasHostile && ((in.simTicks + in.slotIndex * 65) % 240) < 10 && in.energyRatio > 0.65f)",
            ),
            "25_landing_margin_not_lowered": (
                "const float floorMargin = landingSafe ? 2.2f : ((in.curPitch > 0.12f) ? 13.5f : 10.5f);",
                "const float floorMargin = (in.curPitch > 0.12f) ? 13.5f : 10.5f;",
            ),
            "26_post_takeoff_smooth_climb_disabled": (
                "const bool postTakeoffSafe = in.postTakeoffClimb && in.groundIsDry && !in.outOfBounds && (altAboveSurf > 2.5f);",
                "const bool postTakeoffSafe = false;",
            ),
        }
        self.assertGreaterEqual(len(mutants), 12)
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertIn(old, ai_src, f"mutant pattern missing: {name}")
                rc, out = self.compile_and_run(ai_src.replace(old, new, 1), name, sanitize=False)
                self.assertNotEqual(rc, 0, f"mutant {name} was NOT caught! output:\n{out}")


if __name__ == "__main__":
    SCRATCH.mkdir(parents=True, exist_ok=True)
    unittest.main()
