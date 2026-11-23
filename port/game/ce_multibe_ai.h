// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_multibe_ai.h: pure-C++ fair-rules Battle Engine combat AI brain.
// Covers velocity projectile lead, aim-gated sustained/charged/lock-on firing, matchup-aware
// weapon scoring with anti-thrash hysteresis, reactive Walker evasion/dodging with dry-ground
// probes, Jet boom-and-zoom flight with terrain/water pull-up protection, base-defense threat
// prioritization, contested battle-line pressure, and low-HP repair/energy survival rules.
// Zero game-header dependencies so host ASan/UBSan and causal-mutant tests compile this directly.
#ifndef PORT_CE_MULTIBE_AI_H
#define PORT_CE_MULTIBE_AI_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

struct CeAiVec3
{
	float x;
	float y;
	float z;
};

inline bool CeAiIsFinite(float v)
{
	return isfinite(v) != 0;
}

inline bool CeAiIsFiniteVec(const CeAiVec3 &v)
{
	return CeAiIsFinite(v.x) && CeAiIsFinite(v.y) && CeAiIsFinite(v.z);
}

inline float CeAiClamp(float v, float lo, float hi)
{
	if (!CeAiIsFinite(v))
		return 0.0f;
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

inline float CeAiWrapAngle(float a)
{
	if (!CeAiIsFinite(a))
		return 0.0f;
	const float r = remainderf(a, 6.283185307179586f);
	return CeAiIsFinite(r) ? r : 0.0f;
}

// Matches BEA FVector::Azimuth(): forward vector is (-sin(yaw), cos(yaw), 0), so azimuth = -atan2f(dx, dy).
inline float CeAiAzimuth(float dx, float dy)
{
	if (!CeAiIsFinite(dx) || !CeAiIsFinite(dy))
		return 0.0f;
	if (dx * dx + dy * dy <= 1e-8f)
		return 0.0f;
	return -atan2f(dx, dy);
}

// Matches BEA FVector::Elevation(): +Z is DOWN, so negative elevation means target is ABOVE (nose UP).
inline float CeAiElevation(float dx, float dy, float dz)
{
	if (!CeAiIsFinite(dx) || !CeAiIsFinite(dy) || !CeAiIsFinite(dz))
		return 0.0f;
	const float len = sqrtf(dx * dx + dy * dy + dz * dz);
	if (len <= 1e-5f)
		return 0.0f;
	return asinf(CeAiClamp(dz / len, -1.0f, 1.0f));
}

// ---- 1. Velocity Projectile Lead (2-iteration intercept solver) ----

// Computes a 2-iteration projectile intercept point for a weapon firing at `projSpeed` (m/s)
// against a target at `tgtPos` moving at `tgtVel` (m/s).
// - Beams (`projSpeed >= 950 m/s`), invalid/zero speeds, NaNs, and stationary targets return `tgtPos`.
// - Clamps intercept flight time to [0, maxLeadSec] (default 2.2s) to avoid wild over-leading at extreme range.
inline CeAiVec3 CeMultiBeComputeLeadPoint(const CeAiVec3 &myPos, const CeAiVec3 &tgtPos,
                                          const CeAiVec3 &tgtVel, float projSpeed,
                                          float maxLeadSec = 2.2f)
{
	if (!CeAiIsFiniteVec(tgtPos))
	{
		CeAiVec3 zero = { 0.0f, 0.0f, 0.0f };
		return zero;
	}
	if (!CeAiIsFiniteVec(myPos) || !CeAiIsFiniteVec(tgtVel) || !CeAiIsFinite(projSpeed))
		return tgtPos;
	// Zero/negative projectile speed or instantaneous beam (>= 950 m/s): aim directly at target.
	if (projSpeed <= 1.0f || projSpeed >= 950.0f)
		return tgtPos;
	const float spdSq = tgtVel.x * tgtVel.x + tgtVel.y * tgtVel.y + tgtVel.z * tgtVel.z;
	if (spdSq < 0.25f)
		return tgtPos;

	const float maxT = CeAiClamp(maxLeadSec, 0.0f, 3.0f);
	CeAiVec3 aim = tgtPos;
	for (int iter = 0; iter < 2; ++iter)
	{
		const float dx = aim.x - myPos.x;
		const float dy = aim.y - myPos.y;
		const float dz = aim.z - myPos.z;
		const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
		const float t = CeAiClamp(dist / projSpeed, 0.0f, maxT);
		aim.x = tgtPos.x + tgtVel.x * t;
		aim.y = tgtPos.y + tgtVel.y * t;
		aim.z = tgtPos.z + tgtVel.z * t;
	}
	return CeAiIsFiniteVec(aim) ? aim : tgtPos;
}

// ---- 2. Aim-Gated Sustained, Charged & Lock-On Firing ----

enum CeMultiBeFireAction
{
	CE_AI_FIRE_HOLD = 0,
	CE_AI_FIRE_SHOOT = 1,
	CE_AI_FIRE_CHARGE = 2
};

struct CeMultiBeFireGateInput
{
	bool  hasHostile;
	bool  isJet;
	bool  weaponActive;
	bool  readyToFire;
	bool  readyToCharge;
	bool  canCharge;
	bool  fullyCharged;
	float chargeRatio;       // 0..1
	bool  isEnergyWeapon;
	bool  isOverheated;
	float heatOrAmmoRatio;   // energy: 0=cool..1=overheat; ballistic: 0=empty..1=full
	int   ammoCount;         // ballistic count (ignored if isEnergyWeapon)
	bool  isLockWeapon;
	int   activeLocks;
	bool  locksFinished;
	bool  emergencyPullUp;
	float targetDist;
	float minRange;
	float maxRange;
	float yawErrAbs;
	float pitchErrAbs;
	float maxYawCone;
	float maxPitchCone;
	bool  heatCoolingPause;  // latched heat-management pause state
};

struct CeMultiBeFireDecision
{
	CeMultiBeFireAction action;
	bool                nextHeatPause;
};

inline CeMultiBeFireDecision CeMultiBeEvaluateFireGate(const CeMultiBeFireGateInput &in)
{
	CeMultiBeFireDecision out = { CE_AI_FIRE_HOLD, in.heatCoolingPause };

	// Update anti-overheat burst hysteresis for energy weapons before gating.
	if (in.isEnergyWeapon)
	{
		if (in.isOverheated)
			out.nextHeatPause = true;
		else if (in.heatCoolingPause)
			out.nextHeatPause = (in.heatOrAmmoRatio > 0.62f);
		else if (in.heatOrAmmoRatio >= 0.88f && !(in.canCharge && in.chargeRatio >= 0.50f))
			out.nextHeatPause = true;
	}
	else
	{
		out.nextHeatPause = false;
	}

	if (!in.hasHostile || !in.weaponActive || in.isOverheated || in.emergencyPullUp)
		return out;
	if (!in.isEnergyWeapon && in.ammoCount <= 0)
		return out;
	if (out.nextHeatPause)
		return out;
	if (!CeAiIsFinite(in.targetDist) || !CeAiIsFinite(in.yawErrAbs) || !CeAiIsFinite(in.pitchErrAbs))
		return out;

	const float effMin = (in.minRange > 0.0f && CeAiIsFinite(in.minRange)) ? in.minRange : 0.0f;
	const float effMax = (in.maxRange > 5.0f && CeAiIsFinite(in.maxRange)) ? in.maxRange : 220.0f;
	const bool lockedReady = in.isLockWeapon && in.activeLocks > 0 && in.locksFinished;
	if (!lockedReady && (in.targetDist < effMin || in.targetDist > effMax * 1.05f))
		return out;

	const float yawCone = (in.maxYawCone > 0.02f) ? in.maxYawCone : (in.isJet ? 0.28f : 0.22f);
	const float pitchCone = (in.maxPitchCone > 0.02f) ? in.maxPitchCone : (in.isJet ? 0.30f : 0.25f);

	const float effYawCone = lockedReady ? (yawCone * 1.65f) : yawCone;
	const float effPitchCone = lockedReady ? (pitchCone * 1.65f) : pitchCone;

	const bool aimed = (in.yawErrAbs <= effYawCone) && (in.pitchErrAbs <= effPitchCone);
	if (!aimed)
	{
		// Keep holding an already-started charge if nearly aligned so we unleash as crosshair crosses target.
		if (in.canCharge && in.chargeRatio > 0.05f && !in.fullyCharged && in.readyToCharge &&
		    in.yawErrAbs <= yawCone * 1.85f && in.pitchErrAbs <= pitchCone * 1.85f)
		{
			out.action = CE_AI_FIRE_CHARGE;
		}
		return out;
	}

	// Lock-on weapon: wait briefly while an active lock is finishing unless target is close range.
	if (in.isLockWeapon && in.activeLocks > 0 && !in.locksFinished && in.targetDist > 32.0f)
		return out;

	// Charge weapon: charge up, then release when fully charged or tightly aligned at >= 40% charge.
	if (in.canCharge)
	{
		const bool releaseCharge = in.fullyCharged ||
		                           (in.chargeRatio >= 0.40f && in.yawErrAbs <= yawCone * 0.72f &&
		                            in.pitchErrAbs <= pitchCone * 0.72f);
		if (releaseCharge && in.readyToFire)
			out.action = CE_AI_FIRE_SHOOT;
		else if (in.readyToCharge && !in.fullyCharged)
			out.action = CE_AI_FIRE_CHARGE;
		return out;
	}

	// Rapid/direct weapon: pull trigger whenever ReadyToFire() is true for real sustained bursts.
	if (in.readyToFire)
		out.action = CE_AI_FIRE_SHOOT;
	return out;
}

// ---- 3. Matchup-Aware Smart Weapon Selection ----

enum CeAiTargetClass
{
	CE_AI_TGT_INFANTRY = 0,
	CE_AI_TGT_VEHICLE = 1,
	CE_AI_TGT_HEAVY_ARMOR = 2,
	CE_AI_TGT_TURRET = 3,
	CE_AI_TGT_BUILDING = 4,
	CE_AI_TGT_AIRCRAFT = 5,
	CE_AI_TGT_BATTLE_ENGINE = 6
};

struct CeMultiBeWeaponInfo
{
	int   slotIndex;
	bool  isActive;
	bool  isEnergy;
	bool  isOverheated;
	int   ammoCount;
	float heatRatio;
	float minRange;
	float maxRange;
	float launchSpeed;
	float damage;
	float power;
	float reloadTime;
	int   burstSize;
	int   volleySize;
	bool  isBeam;
	bool  isHoming;
	bool  isMissile;
	bool  isExplosive;
	bool  isFlak;
	bool  isBallistic;
	bool  isTorpedo;
	bool  canCharge;
	bool  canLockTarget;
	bool  targetTypeOk;
};

struct CeMultiBeWeaponTargetCtx
{
	CeAiTargetClass targetClass;
	float           targetDist;
	float           targetAlt;
	bool            targetInWater;
	bool            isJetMode;
};

inline float CeMultiBeScoreWeapon(const CeMultiBeWeaponInfo &w, const CeMultiBeWeaponTargetCtx &ctx)
{
	if (!w.isActive || !w.targetTypeOk)
		return -1000.0f;
	if (!w.isEnergy && w.ammoCount <= 0)
		return -1000.0f;
	if (w.isTorpedo && !ctx.targetInWater)
		return -1000.0f;

	float score = 50.0f;

	if (w.isEnergy && w.isOverheated)
		score -= 320.0f;
	else if (w.isEnergy && w.heatRatio > 0.85f)
		score -= 35.0f;

	const float effMin = (w.minRange > 0.0f && CeAiIsFinite(w.minRange)) ? w.minRange : 0.0f;
	const float effMax = (w.maxRange > 5.0f && CeAiIsFinite(w.maxRange)) ? w.maxRange : 220.0f;
	if (ctx.targetDist < effMin)
		score -= 260.0f;
	else if (ctx.targetDist > effMax)
		score -= 260.0f + CeAiClamp((ctx.targetDist - effMax) * 0.4f, 0.0f, 80.0f);
	else
		score += 25.0f;

	const int burst = (w.burstSize > 1) ? w.burstSize : 1;
	const int volley = (w.volleySize > 1) ? w.volleySize : 1;
	const float reload = (w.reloadTime > 0.05f && CeAiIsFinite(w.reloadTime)) ? w.reloadTime : 0.18f;
	const float dmg = (w.damage > 0.0f && CeAiIsFinite(w.damage)) ? w.damage : 1.0f;
	const float dpsEst = (dmg * (float)(burst * volley)) / reload;
	score += CeAiClamp(dpsEst * 1.15f, 0.0f, 34.0f);
	score += CeAiClamp(w.power * 18.0f, 0.0f, 14.0f);

	const bool airborne = (ctx.targetClass == CE_AI_TGT_AIRCRAFT || ctx.targetAlt > 8.0f);
	if (airborne)
	{
		if (w.canLockTarget || w.isHoming)
			score += 48.0f;
		if (w.isBeam || w.launchSpeed >= 120.0f)
			score += 34.0f;
		if (w.isFlak)
			score += 24.0f;
		if (w.isBallistic)
			score -= 55.0f;
		else if (!w.isHoming && !w.isBeam && w.launchSpeed > 0.0f && w.launchSpeed < 55.0f)
			score -= 22.0f;
	}
	else if (ctx.targetClass == CE_AI_TGT_BATTLE_ENGINE ||
	         ctx.targetClass == CE_AI_TGT_HEAVY_ARMOR ||
	         ctx.targetClass == CE_AI_TGT_TURRET ||
	         ctx.targetClass == CE_AI_TGT_BUILDING)
	{
		if (w.isExplosive || w.isMissile || w.canCharge || w.damage >= 8.0f || w.power >= 0.25f)
			score += 36.0f;
		if (w.canLockTarget && ctx.targetClass == CE_AI_TGT_BATTLE_ENGINE)
			score += 18.0f;
		if (w.isBallistic && (ctx.targetClass == CE_AI_TGT_BUILDING || ctx.targetClass == CE_AI_TGT_TURRET) &&
		    ctx.targetDist >= effMin && ctx.targetDist <= effMax)
			score += 18.0f;
	}
	else if (ctx.targetClass == CE_AI_TGT_INFANTRY)
	{
		if (w.isExplosive || w.isFlak)
			score += 32.0f;
		if (reload <= 0.28f || burst >= 3 || volley >= 3)
			score += 26.0f;
		if (w.isEnergy)
			score += 10.0f;	// conserve finite rockets/bombs when energy cannons clear infantry efficiently
		if (w.canCharge && reload > 1.0f)
			score -= 18.0f;
	}
	else
	{
		// Medium ground vehicles / naval
		if (w.isExplosive || w.isBeam || w.damage >= 4.0f)
			score += 18.0f;
		if (reload <= 0.35f)
			score += 12.0f;
		if (w.isTorpedo && ctx.targetInWater)
			score += 35.0f;
	}

	return score;
}

// Selects the optimal weapon slot with anti-thrash hysteresis.
// - Immediately switches if the current weapon is unusable/overheated/out-of-range (score < -40)
//   and a significantly better alternative exists.
// - Voluntary tactical switches require `ticksSinceSwitch >= minSwitchCooldown` and a clear score advantage.
inline int CeMultiBePickWeapon(const CeMultiBeWeaponInfo *weapons, int count, int currentSlot,
                               const CeMultiBeWeaponTargetCtx &ctx, int ticksSinceSwitch,
                               int minSwitchCooldown = 50, float hysteresisMargin = 14.0f)
{
	if (!weapons || count <= 0)
		return currentSlot;

	int bestSlot = currentSlot;
	float bestScore = -2000.0f;
	float curScore = -2000.0f;
	bool foundCurrent = false;

	for (int i = 0; i < count; ++i)
	{
		const float s = CeMultiBeScoreWeapon(weapons[i], ctx);
		if (weapons[i].slotIndex == currentSlot)
		{
			curScore = s;
			foundCurrent = true;
		}
		if (s > bestScore)
		{
			bestScore = s;
			bestSlot = weapons[i].slotIndex;
		}
	}

	if (!foundCurrent)
		return bestSlot;
	if (bestSlot == currentSlot)
		return currentSlot;

	// Immediate switch when current weapon is empty, overheated, inactive, or out-of-range.
	if (curScore < -40.0f && bestScore > curScore + 20.0f)
		return bestSlot;

	// Anti-thrash hysteresis for voluntary tactical switches.
	if (ticksSinceSwitch >= minSwitchCooldown && bestScore > curScore + hysteresisMargin)
		return bestSlot;

	return currentSlot;
}

// ---- 4. Reactive Damage Tracker & Fair-Rules Walker Dash State Machine ----

struct CeMultiBeDamageState
{
	float lastLife;
	float lastDamageTime;
	float recentDamage;       // decaying accumulated damage fraction (0..1+)
	int   underFireTicks;     // countdown ticks remaining in reactive evasion window
	bool  heavyHitPending;    // consumed when evasion/dash responds

	void Reset(float initialLife = 0.0f)
	{
		lastLife = initialLife;
		lastDamageTime = -100.0f;
		recentDamage = 0.0f;
		underFireTicks = 0;
		heavyHitPending = false;
	}

	void Update(float curLife, float maxLife, float engineLastDamageTime, float now)
	{
		if (underFireTicks > 0)
			underFireTicks--;
		recentDamage *= 0.94f;
		if (recentDamage < 0.001f)
			recentDamage = 0.0f;

		const float denom = (maxLife > 1.0f && CeAiIsFinite(maxLife)) ? maxLife : 100.0f;
		float dmgRatio = 0.0f;
		if (CeAiIsFinite(curLife) && CeAiIsFinite(lastLife) && lastLife > 0.0f && curLife < lastLife)
			dmgRatio = (lastLife - curLife) / denom;
		lastLife = CeAiIsFinite(curLife) ? curLife : lastLife;

		const bool timestampHit = CeAiIsFinite(engineLastDamageTime) && CeAiIsFinite(now) &&
		                          (engineLastDamageTime > lastDamageTime + 0.001f) &&
		                          (now - engineLastDamageTime <= 0.75f);
		if (timestampHit)
			lastDamageTime = engineLastDamageTime;

		if (dmgRatio > 0.002f || timestampHit)
		{
			recentDamage += (dmgRatio > 0.002f) ? dmgRatio : 0.025f;
			underFireTicks = 30;	// 1.5s reactive evasion window at 20 Hz
			if (dmgRatio >= 0.055f || recentDamage >= 0.11f)
				heavyHitPending = true;
		}
	}
};

enum CeAiDashPhase
{
	CE_AI_DASH_IDLE = 0,
	CE_AI_DASH_ARMED = 1
};

struct CeMultiBeWalkerDashState
{
	CeAiDashPhase phase;
	int           dashDir;        // -1 = left, +1 = right
	float         armTime;        // EVENT_MANAGER time when first opposite hard push was sent
	int           cooldownTicks;  // ticks remaining before next dash allowed

	void Reset()
	{
		phase = CE_AI_DASH_IDLE;
		dashDir = 0;
		armTime = -100.0f;
		cooldownTicks = 0;
	}
};

struct CeMultiBeWalkerDashOutput
{
	bool  overrideStrafe;   // true while dash state machine owns the lateral strafe axis
	float strafeValue;      // <0 -> StrafeLeft(val), >0 -> StrafeRight(val), 0 -> neutral quiet tick
	bool  dashTriggered;    // true on the tick the second hard push fires the dash
};

// Fair-rules Walker Dash controller matching retail CBattleEngineWalkerPart::StrafeLeft/StrafeRight:
// - Retail dashes toward `dashDir` when a hard opposite push (>0.9) occurred 0.10s..0.20s before a hard
//   push (>0.8) toward `dashDir` (with mDoingDashCount == 0 and onGround == true).
// - Caller must verify dry terrain clearance at 8m and 16m before passing `destDryAndSafe = true`.
inline CeMultiBeWalkerDashOutput CeMultiBeStepWalkerDash(
	CeMultiBeWalkerDashState &st, bool wantDash, int preferredDir,
	bool onGround, int engineDashCount, bool destDryAndSafe, float now)
{
	CeMultiBeWalkerDashOutput out = { false, 0.0f, false };
	if (st.cooldownTicks > 0)
		st.cooldownTicks--;

	if (!CeAiIsFinite(now))
	{
		st.phase = CE_AI_DASH_IDLE;
		return out;
	}

	if (st.phase == CE_AI_DASH_ARMED)
	{
		const float dt = now - st.armTime;
		if (!onGround || engineDashCount > 0 || dt < 0.0f || dt > 0.205f)
		{
			st.phase = CE_AI_DASH_IDLE;
			return out;
		}
		if (dt < 0.11f)
		{
			// Hold lateral axis quiet until inside the (0.10s, 0.20s) retail dash window.
			out.overrideStrafe = true;
			out.strafeValue = 0.0f;
			return out;
		}
		if (dt <= 0.195f && destDryAndSafe)
		{
			out.overrideStrafe = true;
			out.strafeValue = (st.dashDir < 0) ? -0.96f : 0.96f;
			out.dashTriggered = true;
			st.phase = CE_AI_DASH_IDLE;
			st.cooldownTicks = 65;	// 3.25s cooldown between dashes
			return out;
		}
		st.phase = CE_AI_DASH_IDLE;
		return out;
	}

	// IDLE: initiate opposite hard-push if triggered and safe.
	if (wantDash && onGround && engineDashCount == 0 && st.cooldownTicks <= 0 && destDryAndSafe)
	{
		const int dir = (preferredDir < 0) ? -1 : 1;
		st.phase = CE_AI_DASH_ARMED;
		st.dashDir = dir;
		st.armTime = now;
		out.overrideStrafe = true;
		// First push is OPPOSITE to the desired dash direction (retail StrafeLeft/StrafeRight flick rule).
		out.strafeValue = (dir < 0) ? 0.96f : -0.96f;
	}
	return out;
}

// ---- 5. Walker Optimal Range-Keeping & Reactive Evasion ----

struct CeMultiBeWalkerMoveInput
{
	bool  hasHostile;
	bool  isRetreating;
	bool  onRepairPad;
	bool  defensiveStandoff;  // low HP with no repair facility: fight at longer range
	bool  nearWaterHazard;
	bool  outOfBounds;
	float targetDist;
	float goalDist;
	float yawErrToGoal;       // radians
	bool  underFire;
	bool  heavyHit;
	int   strafeDir;          // -1 left, +1 right
	int   strafeTicks;        // ticks spent in current strafe direction
	bool  dryFwd;             // 10m forward probe is dry & in bounds
	bool  dryBack;            // 10m backward probe is dry & in bounds
	bool  dryLeft;            // 10m left probe is dry & in bounds
	bool  dryRight;           // 10m right probe is dry & in bounds
	bool  dryDashLeft;        // 8m & 16m left probes are dry & in bounds
	bool  dryDashRight;       // 8m & 16m right probes are dry & in bounds
};

struct CeMultiBeWalkerMoveCmd
{
	float forwardInput;       // [-0.82, 0.0]: negative walks FORWARD in BEA WalkerPart::Forward(vy)
	float backwardInput;      // [0.0, 0.82]: positive walks BACKWARD in BEA WalkerPart::Backward(vy)
	float strafeInput;        // [-0.82, +0.82]: <0 calls StrafeLeft(val), >0 calls StrafeRight(val)
	int   nextStrafeDir;
	int   nextStrafeTicks;
	bool  reversedStrafe;
	bool  wantDash;
	int   dashDir;
};

inline CeMultiBeWalkerMoveCmd CeMultiBeComputeWalkerMove(const CeMultiBeWalkerMoveInput &in)
{
	CeMultiBeWalkerMoveCmd cmd = { 0.0f, 0.0f, 0.0f, (in.strafeDir < 0) ? -1 : 1, in.strafeTicks + 1, false, false, 0 };

	// Hold still on a friendly repair pad while healing/rearming.
	if (in.isRetreating && in.onRepairPad)
		return cmd;

	// Retreat toward repair pad / friendly base cover (or escape water/map edge).
	if (in.isRetreating || in.nearWaterHazard || in.outOfBounds)
	{
		if (fabsf(in.yawErrToGoal) < 0.95f && in.dryFwd)
			cmd.forwardInput = -0.82f;
		else if (fabsf(in.yawErrToGoal) > 2.2f && in.dryBack)
			cmd.backwardInput = 0.72f;
		if (in.underFire)
		{
			if (cmd.nextStrafeTicks >= 16)
			{
				cmd.nextStrafeDir = -cmd.nextStrafeDir;
				cmd.nextStrafeTicks = 0;
				cmd.reversedStrafe = true;
			}
			const bool canSide = (cmd.nextStrafeDir < 0) ? in.dryLeft : in.dryRight;
			if (canSide)
				cmd.strafeInput = (cmd.nextStrafeDir < 0) ? -0.62f : 0.62f;
		}
		return cmd;
	}

	// No hostile target: march briskly toward tactical objective (contested battle-line / enemy base).
	if (!in.hasHostile)
	{
		if (in.goalDist > 6.0f && fabsf(in.yawErrToGoal) < 0.95f && in.dryFwd)
			cmd.forwardInput = -0.78f;
		return cmd;
	}

	// Combat maneuvering: reactive strafe reversal + Walker Dodge + optimal standoff range-keeping.
	if (in.underFire && in.strafeTicks >= 12)
	{
		cmd.nextStrafeDir = -cmd.nextStrafeDir;
		cmd.nextStrafeTicks = 0;
		cmd.reversedStrafe = true;
	}
	else if (in.strafeTicks >= 55)
	{
		// Periodic unpredictable strafe switch so human players cannot trivialize aiming.
		cmd.nextStrafeDir = -cmd.nextStrafeDir;
		cmd.nextStrafeTicks = 0;
	}

	// Pick a dry strafe direction (never strafe into water!).
	bool chosenDry = (cmd.nextStrafeDir < 0) ? in.dryLeft : in.dryRight;
	if (!chosenDry)
	{
		const int altDir = -cmd.nextStrafeDir;
		const bool altDry = (altDir < 0) ? in.dryLeft : in.dryRight;
		if (altDry)
		{
			cmd.nextStrafeDir = altDir;
			cmd.nextStrafeTicks = 0;
			chosenDry = true;
		}
	}

	// Request a genuine Walker Dash when taking heavy fire (or sustained fire at close-medium range).
	if (in.heavyHit || (in.underFire && in.targetDist < 75.0f && cmd.reversedStrafe))
	{
		const bool prefDashDry = (cmd.nextStrafeDir < 0) ? in.dryDashLeft : in.dryDashRight;
		const bool altDashDry = (cmd.nextStrafeDir < 0) ? in.dryDashRight : in.dryDashLeft;
		if (prefDashDry)
		{
			cmd.wantDash = true;
			cmd.dashDir = cmd.nextStrafeDir;
		}
		else if (altDashDry)
		{
			cmd.wantDash = true;
			cmd.dashDir = -cmd.nextStrafeDir;
		}
	}

	const float minStandoff = in.defensiveStandoff ? 52.0f : 35.0f;
	const float maxStandoff = in.defensiveStandoff ? 105.0f : 85.0f;
	const float crowdDist = in.defensiveStandoff ? 34.0f : 22.0f;

	if (in.targetDist > maxStandoff)
	{
		// Advance into optimal combat envelope while weaving if under fire.
		if (in.dryFwd)
			cmd.forwardInput = -0.80f;
		if (chosenDry)
			cmd.strafeInput = (cmd.nextStrafeDir < 0) ? (in.underFire ? -0.58f : -0.32f)
			                                          : (in.underFire ?  0.58f :  0.32f);
	}
	else if (in.targetDist < crowdDist)
	{
		// Crowded: backpedal & hard lateral circle-strafe.
		if (in.dryBack)
			cmd.backwardInput = 0.74f;
		if (chosenDry)
			cmd.strafeInput = (cmd.nextStrafeDir < 0) ? -0.78f : 0.78f;
	}
	else
	{
		// Optimal standoff combat band (35..85m): active circle-strafing with subtle depth control.
		if (chosenDry)
			cmd.strafeInput = (cmd.nextStrafeDir < 0) ? (in.underFire ? -0.80f : -0.68f)
			                                          : (in.underFire ?  0.80f :  0.68f);
		if (in.targetDist > (minStandoff + maxStandoff) * 0.55f && in.dryFwd)
			cmd.forwardInput = -0.40f;
		else if (in.targetDist < minStandoff && in.dryBack)
			cmd.backwardInput = 0.40f;
	}

	return cmd;
}

// ---- 6. Jet Boom-and-Zoom & Evasive Flight Control ----

enum CeAiJetPhase
{
	CE_AI_JET_CRUISE = 0,
	CE_AI_JET_ATTACK_RUN = 1,
	CE_AI_JET_BREAKOFF = 2
};

struct CeMultiBeJetFlightInput
{
	float        curYaw;
	float        curPitch;        // BEA convention: <0 is NOSE UP, >0 is NOSE DOWN
	float        posZ;            // BEA convention: +Z is DOWN
	float        groundZ;
	float        aheadGroundZ;    // terrain Z probed ~28m ahead along flight path
	float        waterZ;
	CeAiVec3     toGoal;          // vector to navigation goal (or led target during attack run)
	bool         hasHostile;
	bool         isRetreating;
	bool         underHeavyFire;
	bool         outOfBounds;
	float        desiredCruiseHeight;
	CeAiJetPhase prevPhase;
	int          phaseTicks;
	int          breakTurnDir;    // -1 or +1
};

struct CeMultiBeJetFlightCmd
{
	float        turnInput;       // passed directly to jet->Turn(turnInput)
	float        pitchInput;      // passed directly to jet->Pitch(pitchInput): >0 pulls nose UP!
	float        thrustVy;        // passed directly to jet->Thrust(thrustVy), safely clamped from loop trigger
	float        yawErrAbs;       // absolute yaw alignment error to target (radians)
	float        pitchErrAbs;     // absolute pitch alignment error to target (radians)
	bool         emergencyPullUp;
	bool         inAttackRun;
	CeAiJetPhase nextPhase;
	int          nextPhaseTicks;
	int          nextBreakTurnDir;
	bool         startedBreakoff;
};

inline CeMultiBeJetFlightCmd CeMultiBeComputeJetFlight(const CeMultiBeJetFlightInput &in)
{
	CeMultiBeJetFlightCmd cmd = {};
	cmd.nextPhase = in.prevPhase;
	cmd.nextPhaseTicks = in.phaseTicks + 1;
	cmd.nextBreakTurnDir = (in.breakTurnDir < 0) ? -1 : 1;

	// Effective surface height is the HIGHEST (most negative Z) of ground under us, ground ahead, and water.
	float surfaceZ = in.waterZ;
	if (CeAiIsFinite(in.groundZ) && in.groundZ < surfaceZ)
		surfaceZ = in.groundZ;
	if (CeAiIsFinite(in.aheadGroundZ) && in.aheadGroundZ < surfaceZ)
		surfaceZ = in.aheadGroundZ;
	if (!CeAiIsFinite(surfaceZ))
		surfaceZ = 0.0f;

	const float cruiseHeight = (in.desiredCruiseHeight > 14.0f && CeAiIsFinite(in.desiredCruiseHeight))
		? in.desiredCruiseHeight : 22.0f;
	const float safeCruiseZ = surfaceZ - cruiseHeight;
	const float floorMargin = (in.curPitch > 0.12f) ? 13.5f : 10.5f;
	const float floorDangerZ = surfaceZ - floorMargin;

	const float horizDist = sqrtf(in.toGoal.x * in.toGoal.x + in.toGoal.y * in.toGoal.y);
	const float targetYaw = (horizDist > 1.0f) ? CeAiAzimuth(in.toGoal.x, in.toGoal.y) : in.curYaw;
	const float targetPitch = CeAiElevation(in.toGoal.x, in.toGoal.y, in.toGoal.z);
	cmd.yawErrAbs = fabsf(CeAiWrapAngle(targetYaw - in.curYaw));
	cmd.pitchErrAbs = fabsf(CeAiWrapAngle(targetPitch - in.curPitch));

	// Phase transitions (Boom-and-Zoom).
	if (in.isRetreating || in.outOfBounds || !in.hasHostile)
	{
		cmd.nextPhase = CE_AI_JET_CRUISE;
		cmd.nextPhaseTicks = 0;
	}
	else if (in.prevPhase == CE_AI_JET_BREAKOFF)
	{
		// Extend and climb until clear distance and safe altitude, or max breakoff duration.
		if ((horizDist > 85.0f && in.posZ <= surfaceZ - 18.0f && in.phaseTicks >= 22) || in.phaseTicks >= 55)
		{
			cmd.nextPhase = CE_AI_JET_ATTACK_RUN;
			cmd.nextPhaseTicks = 0;
		}
	}
	else
	{
		// Only break off on a close pass, heavy AA fire, or low pull-up near the target —
		// never turn 180 deg away right after taking off far from the enemy!
		if (horizDist < 38.0f || in.underHeavyFire || (in.posZ > floorDangerZ && horizDist < 52.0f))
		{
			cmd.nextPhase = CE_AI_JET_BREAKOFF;
			cmd.nextPhaseTicks = 0;
			cmd.nextBreakTurnDir = -cmd.nextBreakTurnDir;
			cmd.startedBreakoff = true;
		}
		else if (horizDist <= 250.0f)
		{
			cmd.nextPhase = CE_AI_JET_ATTACK_RUN;
		}
		else
		{
			cmd.nextPhase = CE_AI_JET_CRUISE;
		}
	}

	// Compute desired heading, pitch (BEA: negative = nose UP, positive = nose DOWN), and thrust.
	float desiredYaw = targetYaw;
	float desiredPitch = 0.0f;
	float thrust = 0.80f;

	if (in.posZ > floorDangerZ)
	{
		// Absolute terrain/water collision avoidance pull-up!
		cmd.emergencyPullUp = true;
		desiredPitch = -0.44f;	// strong nose UP (-Z climb!)
		thrust = 1.0f;
		// Bank slightly away from steep ground if diving into a hillside near target.
		if (in.hasHostile && horizDist < 45.0f)
			desiredYaw = CeAiWrapAngle(targetYaw + (float)cmd.nextBreakTurnDir * 0.85f);
	}
	else if (cmd.nextPhase == CE_AI_JET_BREAKOFF)
	{
		// Evasive banked climb-turn away from target/AA fire, resetting for the next pass.
		desiredYaw = CeAiWrapAngle(targetYaw + 3.14159265f + (float)cmd.nextBreakTurnDir * 0.55f);
		const float climbAltZ = surfaceZ - (cruiseHeight + 10.0f);
		const float altErr = in.posZ - climbAltZ;	// >0 means below climbAltZ -> need negative desiredPitch (UP)
		desiredPitch = CeAiClamp(-altErr * 0.028f, -0.36f, 0.10f);
		thrust = 1.0f;
	}
	else if (cmd.nextPhase == CE_AI_JET_ATTACK_RUN)
	{
		cmd.inAttackRun = true;
		desiredYaw = targetYaw;
		// At long closing range (>135m), hold safe cruise altitude above rising terrain;
		// inside 135m, nose smoothly onto the velocity-led target while guarding ground clearance.
		if (horizDist > 135.0f)
		{
			const float altErr = in.posZ - safeCruiseZ;
			desiredPitch = CeAiClamp(-altErr * 0.026f, -0.36f, 0.16f);
		}
		else
		{
			const float altAboveSurf = surfaceZ - in.posZ;
			const float maxSafeDive = (altAboveSurf > 20.0f) ? 0.36f :
			                          (altAboveSurf > 14.5f) ? 0.24f : 0.08f;
			desiredPitch = CeAiClamp(targetPitch, -0.38f, maxSafeDive);
		}
		// Moderate speed in close-in strafing runs for sustained accurate bursts; afterburner when closing.
		thrust = (horizDist > 115.0f) ? 0.98f : 0.66f;
	}
	else
	{
		// Cruise / transit / retreat.
		desiredYaw = targetYaw;
		const float cruiseTargetZ = in.isRetreating ? (surfaceZ - 18.0f) : safeCruiseZ;
		const float altErr = in.posZ - cruiseTargetZ;	// >0 means too low -> need negative desiredPitch (nose UP)
		desiredPitch = CeAiClamp(-altErr * 0.024f, -0.34f, 0.24f);
		thrust = (horizDist > 110.0f || in.isRetreating) ? 0.95f : 0.76f;
	}

	const float yawErr = CeAiWrapAngle(desiredYaw - in.curYaw);
	const float pitchErr = CeAiWrapAngle(desiredPitch - in.curPitch);

	// BEA JetPart::Turn(vx) subtracts vx*rate from mYawvel -> vx = -k * yawErr.
	cmd.turnInput = CeAiClamp(-yawErr * 1.55f, -0.95f, 0.95f);
	// BEA JetPart::Pitch(vy) subtracts vy/117 from mPitchvel -> vy = -k * pitchErr (>0 pulls nose UP!).
	cmd.pitchInput = CeAiClamp(-pitchErr * 1.95f, -0.92f, 0.92f);
	// BEA JetPart::Thrust(vy) sets mThrusterValue = 0.5 - vy/2; clamp away from <-0.9/>0.8 loop trigger.
	cmd.thrustVy = CeAiClamp(1.0f - 2.0f * thrust, -0.84f, 0.52f);
	return cmd;
}

// ---- 7. Tactical Target Prioritization & Contested Battle-Line Pressure ----

struct CeMultiBeTargetCandidate
{
	CeAiTargetClass unitClass;
	float           distToMe;
	float           distToNearestFriendlyBase;  // >= 9999 if no friendly buildings
	bool            friendlyBaseUnderAttack;    // true if friendly building recently damaged
	float           targetThreat;
	float           targetImportance;
	float           targetLifeRatio;            // 0..1
	bool            isAttackingMe;
	bool            isAirborne;
	bool            isCurrentTarget;
};

inline float CeMultiBeScoreTarget(const CeMultiBeTargetCandidate &c)
{
	if (!CeAiIsFinite(c.distToMe) || c.distToMe < 0.0f)
		return -10000.0f;

	float basePriority = 45.0f;
	switch (c.unitClass)
	{
	case CE_AI_TGT_BATTLE_ENGINE: basePriority = 120.0f; break;
	case CE_AI_TGT_HEAVY_ARMOR:   basePriority =  84.0f; break;
	case CE_AI_TGT_AIRCRAFT:      basePriority =  80.0f; break;
	case CE_AI_TGT_TURRET:        basePriority =  72.0f; break;
	case CE_AI_TGT_BUILDING:      basePriority =  68.0f; break;
	case CE_AI_TGT_VEHICLE:       basePriority =  62.0f; break;
	case CE_AI_TGT_INFANTRY:      basePriority =  28.0f; break;
	default:                      basePriority =  45.0f; break;
	}

	float score = basePriority;

	// Base defense urgency: prioritize attackers threatening friendly factories/buildings within 75m!
	if (CeAiIsFinite(c.distToNearestFriendlyBase) && c.distToNearestFriendlyBase < 75.0f)
	{
		score += (75.0f - c.distToNearestFriendlyBase) * 1.45f;
		if (c.friendlyBaseUnderAttack)
			score += 95.0f;
	}

	// Active dueling / self-defense priority.
	if (c.isAttackingMe)
		score += (c.unitClass == CE_AI_TGT_BATTLE_ENGINE) ? 60.0f : 38.0f;

	// Engagement envelope bonus: fight in-range threats immediately rather than flying past them
	// toward a distant out-of-range enemy (while urgent base defense still overrides stray skirmishes).
	if (c.distToMe <= 105.0f)
		score += 55.0f;
	else if (c.distToMe <= 145.0f)
		score += 22.0f;

	// Distance falloff (closer threats remain urgent without overriding base defense).
	score -= c.distToMe * 0.42f;

	// Finish-off bonus for wounded targets so kills are secured cleanly.
	if (CeAiIsFinite(c.targetLifeRatio) && c.targetLifeRatio > 0.0f && c.targetLifeRatio < 0.45f)
		score += (0.45f - c.targetLifeRatio) * 45.0f;

	// Retail threat & importance signals.
	const float tVal = CeAiIsFinite(c.targetThreat) ? c.targetThreat : 0.0f;
	const float iVal = CeAiIsFinite(c.targetImportance) ? c.targetImportance : 0.0f;
	score += CeAiClamp(tVal * 2.5f + iVal * 1.5f, 0.0f, 25.0f);

	// Target persistence hysteresis avoids flickering between near-equal targets.
	if (c.isCurrentTarget)
		score += 14.0f;

	return score;
}

// Scores an INFLUENCEMAP node for front-line advancement when no immediate hostile is in combat range.
// In BEA InfluenceMap.cpp: influence > 0 leans Forseti (side 2), influence < 0 leans Muspell (side 1).
inline float CeMultiBeScoreBattleLineNode(bool selfIsMuspell, float nodeInfluence,
                                          float forsetiThreat, float muspellThreat,
                                          float distToMe)
{
	if (!CeAiIsFinite(distToMe) || !CeAiIsFinite(nodeInfluence))
		return -10000.0f;

	// Convert to self-relative influence: +1 = firmly ours, 0 = contested, -1 = firmly enemy.
	const float relInfluence = selfIsMuspell ? -nodeInfluence : nodeInfluence;
	const float myThreat = selfIsMuspell ? muspellThreat : forsetiThreat;
	const float enemyThreat = selfIsMuspell ? forsetiThreat : muspellThreat;

	float score = 0.0f;
	// Contested frontline nodes (both sides present or neutral/swinging) get top billing!
	if (myThreat > 0.1f && enemyThreat > 0.1f)
		score += 95.0f;
	if (fabsf(relInfluence) < 0.55f)
		score += 65.0f * (1.0f - fabsf(relInfluence) / 0.55f);
	// Enemy-held nodes are good push targets; already firmly friendly rear nodes are penalized.
	if (relInfluence < -0.15f)
		score += 55.0f;
	else if (relInfluence > 0.65f)
		score -= 40.0f;

	// Prefer pushing toward nearby frontline sectors rather than crossing the entire map when a closer front exists.
	score -= distToMe * 0.18f;
	return score;
}

// ---- 8. Survival, Repair-Pad Retreat Hysteresis & Smart Morphing ----

struct CeMultiBeSurvivalInput
{
	float lifeRatio;            // 0..1
	float energyRatio;          // 0..1
	bool  isJet;
	bool  groundIsDry;
	bool  hasRepairPad;
	bool  hasFriendlyBase;
	bool  currentlyRetreating;
};

struct CeMultiBeSurvivalDecision
{
	bool retreating;            // disengage toward repair pad / friendly base cover
	bool defensiveStandoff;     // low HP with no repair facility: fight cautiously at long standoff
	bool seekDryShore;          // low energy in Jet mode over water: fly to dry land before landing
	bool wantSafeEnergyLand;    // low energy in Jet mode over dry ground: land now!
};

inline CeMultiBeSurvivalDecision CeMultiBeEvaluateSurvival(const CeMultiBeSurvivalInput &in)
{
	CeMultiBeSurvivalDecision out = { false, false, false, false };
	const float life = CeAiClamp(in.lifeRatio, 0.0f, 1.0f);
	const float energy = CeAiClamp(in.energyRatio, 0.0f, 1.0f);
	const bool hasSanctuary = in.hasRepairPad || in.hasFriendlyBase;

	// Repair/retreat hysteresis: retreat below 35% HP, return to battle only once healed past 65% HP!
	if (hasSanctuary)
	{
		if (in.currentlyRetreating)
			out.retreating = (life < 0.65f);
		else
			out.retreating = (life < 0.35f);
	}
	else
	{
		out.retreating = false;
		out.defensiveStandoff = (life < 0.40f);
	}

	// Smart energy safety: never drain to 0% over water; land early on dry ground below 32% energy!
	if (in.isJet && energy < 0.32f)
	{
		if (in.groundIsDry)
			out.wantSafeEnergyLand = true;
		else
			out.seekDryShore = true;
	}
	return out;
}

struct CeMultiBeSmartMorphInput
{
	bool  isJet;
	bool  flightAllowed;
	bool  groundIsDry;
	bool  nearWaterHazard;
	float energyRatio;
	bool  hasHostile;
	float targetDist;
	float targetAltitude;
	float goalDist;             // distance to nav/retreat/repair goal
	bool  retreating;
	bool  nearRepairPad;        // within 24m of a friendly repair pad while retreating
	bool  baseDefenseUrgent;    // friendly building under attack
	int   ticksSinceMorph;
	int   slotIndex;
	int   simTicks;
};

// Fair-rules combined-arms Walker <-> Jet morph selector.
// CRITICAL SAFETY INVARIANT: NEVER morph from Jet -> Walker unless `groundIsDry == true`!
inline bool CeMultiBeShouldMorphSmart(const CeMultiBeSmartMorphInput &in)
{
	if (!in.flightAllowed)
		return false;

	if (!in.isJet)
	{
		// Immediate emergency takeoff if walker is dangerously close to water.
		if (in.nearWaterHazard && in.energyRatio > 0.25f)
			return true;
		if (in.energyRatio < 0.45f)
			return false;
		// Urgent scramble to defend friendly base or retreat to a distant repair pad.
		if (in.baseDefenseUrgent && in.targetDist > 75.0f && in.ticksSinceMorph >= 55)
			return true;
		if (in.retreating && !in.nearRepairPad && in.goalDist > 65.0f && in.ticksSinceMorph >= 60)
			return true;
		if (in.ticksSinceMorph < 85)
			return false;
		if (in.energyRatio < 0.50f)
			return false;
		// Take off to intercept airborne targets, close on distant targets/objectives, or run air sorties.
		if (in.hasHostile && in.targetAltitude > 11.0f)
			return true;
		if (in.hasHostile && in.targetDist > 85.0f)
			return true;
		if (!in.hasHostile && in.goalDist > 90.0f)
			return true;
		if (in.hasHostile && ((in.simTicks + in.slotIndex * 65) % 240) < 10 && in.energyRatio > 0.65f)
			return true;
		return false;
	}
	else
	{
		// ABSOLUTE INVARIANT: never morph Jet -> Walker over water!
		if (!in.groundIsDry)
			return false;
		// Land early on dry ground before energy stalls.
		if (in.energyRatio < 0.32f)
			return true;
		// Land promptly when arriving at a friendly repair pad while retreating.
		if (in.retreating && in.nearRepairPad && in.ticksSinceMorph >= 25)
			return true;
		if (in.ticksSinceMorph < 75)
			return false;
		// Drop to Walker mode for precision close-in ground combat over dry terrain.
		if (!in.retreating && in.hasHostile && in.targetDist >= 0.0f &&
		    in.targetDist < 50.0f && in.targetAltitude < 10.0f)
			return true;
		return false;
	}
}

#endif
