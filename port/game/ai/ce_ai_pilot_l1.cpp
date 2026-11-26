// SPDX-License-Identifier: GPL-3.0-only
// port/game/ai/ce_ai_pilot_l1.cpp: Extracted Layer-1 deterministic Battle Engine combat pilot.
// Owns fair-rules Battle Engine target prioritization, velocity-led aiming, weapon scoring/switching,
// Walker dodging/dashing, Jet boom-and-zoom + dry-ground low-altitude landing, and survival retreat.
// Invoked synchronously inside SIM player_act (0396-ce-hook-player-act) via ce_player_hooks.cpp.
#include "ai/ce_ai_pilot_l1.h"
#include "ce_multibe.h"
#include "ce_multibe_ai.h"
#include "ce_policy.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "player.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "Camera.h"
#include "ThingType.h"
#include "unit.h"
#include "UnitAI.h"
#include "weapon.h"
#include "InfluenceMap.h"
#include "eventmanager.h"

#include <string.h>

namespace
{

inline void CeAiNormaliseVec(FVector &v)
{
	const float len = CeAiSqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
	if (len > 1e-4f)
	{
		const float inv = 1.0f / len;
		v.X *= inv;
		v.Y *= inv;
		v.Z *= inv;
	}
}

struct PilotGameAccess : CGame
{
	static CCamera *&CameraSlot(int slot)
	{
		return (GAME.*(&PilotGameAccess::mCurrentCamera))[slot];
	}
};

struct CeMultiBeSlotBrain
{
	CeMultiBeDamageState		damage;
	CeMultiBeWalkerDashState	dash;
	CeAiJetPhase			jetPhase;
	int						jetPhaseTicks;
	int						breakTurnDir;
	int						strafeDir;
	int						strafeTicks;
	bool					retreating;
	bool					seekDryShore;
	bool					heatPause;
	CUnit					*lastTarget;	// pointer-identity only; never dereferenced without live world check
	float					storeVal[kBattleEngineStores];
	bool					storeOverheat[kBattleEngineStores];
	bool					storeTrackerInit;
	int						chargeTicks;
	EBattleEngineState		prevState;
	int						ticksSinceJetEnter;
	int						ticksSinceMorph;
	int						ticksSinceWeaponSwitch;

	void Reset(int slotIdx = 0)
	{
		damage.Reset(0.0f);
		dash.Reset();
		jetPhase = CE_AI_JET_CRUISE;
		jetPhaseTicks = 0;
		breakTurnDir = (slotIdx & 1) ? 1 : -1;
		strafeDir = (slotIdx & 1) ? 1 : -1;
		strafeTicks = 0;
		retreating = false;
		seekDryShore = false;
		heatPause = false;
		lastTarget = NULL;
		for (int s = 0; s < kBattleEngineStores; ++s)
		{
			storeVal[s] = 0.0f;
			storeOverheat[s] = false;
		}
		storeTrackerInit = false;
		chargeTicks = 0;
		prevState = BATTLE_ENGINE_STATE_WALKER;
		ticksSinceJetEnter = 0;
		ticksSinceMorph = 75;
		ticksSinceWeaponSwitch = 50;
	}
};

CeMultiBeSlotBrain gAiBrain[CE_MULTIBE_MAX];

bool UnitExistsInWorld(const CUnit *needle)
{
	if (!needle)
		return false;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
		if (u == needle)
			return true;
	return false;
}

bool ContainsIgnoreCase(const char *haystack, const char *needle)
{
	if (!haystack || !needle || !*needle)
		return false;
	for (const char *p = haystack; *p; ++p)
	{
		const char *a = p;
		const char *b = needle;
		while (*a && *b)
		{
			char ca = *a;
			char cb = *b;
			if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
			if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
			if (ca != cb)
				break;
			++a;
			++b;
		}
		if (!*b)
			return true;
	}
	return false;
}

bool IsDryAndInBounds(const FVector &pos, float waterZ, float margin)
{
	if (!CeMultiBeIsFinitePos(pos.X, pos.Y, pos.Z))
		return false;
	if (pos.X < 36.0f || pos.X > 476.0f || pos.Y < 36.0f || pos.Y > 476.0f)
		return false;
	return CeMultiBeIsAboveWater(MAP.Collide(pos), waterZ, margin);
}

CeAiTargetClass ClassifyUnitTarget(CUnit *u)
{
	if (!u)
		return CE_AI_TGT_VEHICLE;
	if (u->IsA(THING_TYPE_BATTLE_ENGINE))
		return CE_AI_TGT_BATTLE_ENGINE;
	if (u->IsA(THING_TYPE_AIR_UNIT))
		return CE_AI_TGT_AIRCRAFT;
	if (u->IsA(THING_TYPE_BUILDING))
		return CE_AI_TGT_BUILDING;
	if (u->IsA(THING_TYPE_EMPLACEMENT))
		return CE_AI_TGT_TURRET;
	if (u->IsA(THING_TYPE_INFANTRY))
		return CE_AI_TGT_INFANTRY;
	if (u->IsA(THING_TYPE_MECH))
		return CE_AI_TGT_HEAVY_ARMOR;
	if (CUnitData *ud = u->GetUnitData())
	{
		switch (ud->mBehaviour)
		{
		case UB_MECH:
		case UB_THUNDERHEAD:
		case UB_GILLM:
		case UB_FENRIR:
		case UB_WARSPITE:
		case UB_HIVE_BOSS:
			return CE_AI_TGT_HEAVY_ARMOR;
		case UB_TURRET:
		case UB_MINE:
			return CE_AI_TGT_TURRET;
		case UB_BUILDING:
		case UB_SIMPLE_BUILDING:
			return CE_AI_TGT_BUILDING;
		case UB_AIRCRAFT:
		case UB_FIGHTER:
		case UB_BOMBER:
		case UB_GROUND_ATTACK_AIRCRAFT:
		case UB_DIVE_BOMBER:
		case UB_CARVER:
		case UB_CARRIER:
		case UB_DROPSHIP:
			return CE_AI_TGT_AIRCRAFT;
		case UB_INFANTRY:
			return CE_AI_TGT_INFANTRY;
		default:
			break;
		}
	}
	return CE_AI_TGT_VEHICLE;
}

struct CeMultiBeTacticalScan
{
	CUnit  *target;
	CUnit  *nearestHostileAnywhere;
	bool    hasRepairPad;
	FVector repairPadPos;
	float   repairPadDist;
	bool    hasFriendlyBase;
	FVector friendlyBasePos;
	float   friendlyBaseDist;
	bool    baseDefenseUrgent;
};

CeMultiBeTacticalScan GatherTacticalContext(CBattleEngine *self, int idx, float now, const CeStrategicPrior *prior)
{
	CeMultiBeTacticalScan res = {};
	res.repairPadDist = 1e9f;
	res.friendlyBaseDist = 1e9f;
	if (!self)
		return res;

	const FVector myPos = self->GetPos();
	const EAllegiance mySide = self->GetAllegiance();

	enum { kMaxFriendlyBases = 16 };
	FVector basePos[kMaxFriendlyBases];
	bool baseDamaged[kMaxFriendlyBases];
	int baseCount = 0;

	{
		ListIterator<CUnit> it(&WORLD.GetUnitNB());
		for (CUnit *u = it.First(); u; u = it.Next())
		{
			if (!u || u == self || u->IsDying() || !u->Active())
				continue;
			const EAllegiance side = u->GetAllegiance();
			const FVector uPos = u->GetPos();
			if (!CeMultiBeIsFinitePos(uPos.X, uPos.Y, uPos.Z))
				continue;

			if (side == mySide || side == kIndependentAllegiance)
			{
				if (CUnitData *ud = u->GetUnitData())
				{
					if (ud->mName && (ContainsIgnoreCase(ud->mName, "Repair Pad") ||
					                  ContainsIgnoreCase(ud->mName, "Health Pad")))
					{
						const float dx = uPos.X - myPos.X;
						const float dy = uPos.Y - myPos.Y;
						const float d = CeAiSqrt(dx * dx + dy * dy);
						if (d < res.repairPadDist)
						{
							res.hasRepairPad = true;
							res.repairPadPos = uPos;
							res.repairPadDist = d;
						}
					}
				}
			}

			if (side == mySide && u->IsA(THING_TYPE_BUILDING))
			{
				const float dx = uPos.X - myPos.X;
				const float dy = uPos.Y - myPos.Y;
				const float d = CeAiSqrt(dx * dx + dy * dy);
				if (d < res.friendlyBaseDist)
				{
					res.hasFriendlyBase = true;
					res.friendlyBasePos = uPos;
					res.friendlyBaseDist = d;
				}
				if (baseCount < kMaxFriendlyBases)
				{
					bool underAttack = false;
					const float maxLife = u->GetMaxLife();
					if (maxLife > 1.0f && u->GetLife() < maxLife * 0.94f)
						underAttack = true;
					const char *cls = u->_GetClassName();
					if (cls && strcmp(cls, "CBuilding") == 0)
					{
						CBuilding *b = static_cast<CBuilding *>(u);
						if (b->mUnknown268 > 0.0f && (now - b->mUnknown268) <= 6.0f)
							underAttack = true;
					}
					basePos[baseCount] = uPos;
					baseDamaged[baseCount] = underAttack;
					baseCount++;
				}
			}
		}
	}

	CUnit *bestTarget = NULL;
	float bestScore = -1e9f;
	bool bestThreatensBase = false;
	CUnit *nearestAny = NULL;
	float nearestAnyDistSq = 1e12f;

	{
		ListIterator<CUnit> it(&WORLD.GetUnitNB());
		for (CUnit *u = it.First(); u; u = it.Next())
		{
			if (!u || u == self || u->IsDying() || !u->Active())
				continue;
			if (!self->IsTargetAlligence(u->GetAllegiance()))
				continue;
			const FVector uPos = u->GetPos();
			if (!CeMultiBeIsFinitePos(uPos.X, uPos.Y, uPos.Z))
				continue;

			const FVector delta = uPos - myPos;
			const float dSq = delta.MagnitudeSq();
			if (dSq <= 1.0f)
				continue;
			const float dist = CeAiSqrt(dSq);
			if (dSq < nearestAnyDistSq)
			{
				nearestAnyDistSq = dSq;
				nearestAny = u;
			}
			if (!u->OnScanner() && dist > 90.0f)
				continue;

			float minBaseDist = 99999.0f;
			bool baseUnderAttack = false;
			for (int b = 0; b < baseCount; ++b)
			{
				const float bx = uPos.X - basePos[b].X;
				const float by = uPos.Y - basePos[b].Y;
				const float bd = CeAiSqrt(bx * bx + by * by);
				if (bd < minBaseDist)
				{
					minBaseDist = bd;
					baseUnderAttack = baseDamaged[b];
				}
			}

			if (dist > 420.0f && minBaseDist >= 75.0f)
				continue;

			const CeAiTargetClass uClass = ClassifyUnitTarget(u);
			bool attackingMe = false;
			if (CUnitAI *uai = u->GetAI())
			{
				if (uai->mTarget.ToRead() == self)
					attackingMe = true;
			}
			else if (uClass == CE_AI_TGT_BATTLE_ENGINE && dist < 130.0f)
			{
				attackingMe = true;
			}

			const float uMaxLife = u->GetMaxLife();
			const float uLifeRatio = (uMaxLife > 0.01f) ? CeAiClamp(u->GetLife() / uMaxLife, 0.0f, 1.0f) : 1.0f;

			CeMultiBeTargetCandidate cand = {};
			cand.unitClass = uClass;
			cand.distToMe = dist;
			cand.distToNearestFriendlyBase = minBaseDist;
			cand.friendlyBaseUnderAttack = baseUnderAttack;
			cand.targetThreat = u->GetThreat();
			cand.targetImportance = u->GetImportance();
			cand.targetLifeRatio = uLifeRatio;
			cand.isAttackingMe = attackingMe;
			cand.isAirborne = (u->IsA(THING_TYPE_AIR_UNIT) != FALSE);
			cand.isCurrentTarget = (idx >= 0 && idx < CE_MULTIBE_MAX && u == gAiBrain[idx].lastTarget);

			float score = CeMultiBeScoreTarget(cand);
			// Optional Layer-3 strategic prior focus bias (offline only; NULL in lockstep v1).
			if (prior && prior->hasFocusPoint)
			{
				const float fx = uPos.X - prior->focusX;
				const float fy = uPos.Y - prior->focusY;
				if (fx * fx + fy * fy <= 85.0f * 85.0f)
					score += 14.0f;
			}
			if (score > bestScore)
			{
				bestScore = score;
				bestTarget = u;
				bestThreatensBase = (minBaseDist < 75.0f);
			}
		}
	}

	res.target = bestTarget;
	res.nearestHostileAnywhere = nearestAny;
	res.baseDefenseUrgent = bestThreatensBase;
	return res;
}

bool FindBattleLineGoal(CBattleEngine *self, const FVector &myPos, FVector *outGoal)
{
	if (!self || !outGoal || !INFLUENCEMAP || INFLUENCEMAP->IsEmpty())
		return false;
	const bool selfIsMuspell = (self->GetAllegiance() == kMuspellAllegiance);
	float bestScore = -1e9f;
	bool found = false;
	FVector bestPos(256.0f, 256.0f, myPos.Z);

	ListIterator<CInfluenceNode> it(&INFLUENCEMAP->mNodes);
	for (CInfluenceNode *n = it.First(); n; n = it.Next())
	{
		if (!n)
			continue;
		const float nx = n->GetX();
		const float ny = n->GetY();
		if (!CeAiIsFinite(nx) || !CeAiIsFinite(ny) || nx < 40.0f || nx > 472.0f || ny < 40.0f || ny > 472.0f)
			continue;
		const float dx = nx - myPos.X;
		const float dy = ny - myPos.Y;
		const float dist = CeAiSqrt(dx * dx + dy * dy);
		const float score = CeMultiBeScoreBattleLineNode(selfIsMuspell, n->mInfluence, n->mThreat[0], n->mThreatB[0], dist);
		if (score > bestScore)
		{
			bestScore = score;
			bestPos = FVector(nx, ny, MAP.Collide(FVector(nx, ny, 0.0f)));
			found = true;
		}
	}
	if (found)
		*outGoal = bestPos;
	return found;
}

enum CeAiWeaponCallKind
{
	CE_AI_CALL_FIRE = 0,
	CE_AI_CALL_CHARGE = 1,
	CE_AI_CALL_LOCKS = 2
};

void InvokeAiWeaponCall(int idx, CBattleEngine *be, CeAiWeaponCallKind kind)
{
	if (!be || idx < 0 || idx >= MAX_PLAYERS)
		return;
	if (CeMultiBeAiHostSlotIsAux(idx))
	{
		CCamera *auxCam = CeMultiBeAiHostSlotAuxCamera(idx);
		if (!auxCam)
			return;
		CCamera *savedCam = PilotGameAccess::CameraSlot(idx);
		PilotGameAccess::CameraSlot(idx) = auxCam;
		CUnit *xUnit = NULL;
		if (kind == CE_AI_CALL_FIRE || kind == CE_AI_CALL_LOCKS)
			xUnit = be->CalcUnitOverCrossHair(NULL, TRUE, TRUE);
		if (kind == CE_AI_CALL_FIRE)
		{
			be->FireWeapon();
		}
		else if (kind == CE_AI_CALL_CHARGE)
		{
			be->ChargeWeapon();
		}
		else
		{
			be->HandleLocks();
			CWeapon *w = be->GetCurrentWeapon();
			if (xUnit && w && w->GetLockMode() == kSequenceLockMode && w->ReadyToFire() &&
			    w->GetCurrentMode() != NULL && be->LocksFinished() &&
			    be->CountLocks() < w->GetMaxLocks() &&
			    be->IsTargetAlligence(xUnit->GetAllegiance()) &&
			    w->CanLock(xUnit) && !be->Locked(xUnit))
			{
				const FVector path = xUnit->GetPos() - be->GetPos();
				const float lockRange = w->GetLockRange();
				if (path.MagnitudeSq() > 1e-4f && path.MagnitudeSq() < lockRange * lockRange)
				{
					FMatrix invOri = be->GetOrientation();
					invOri.TransposeInPlace();
					FVector heading = invOri * path;
					CeAiNormaliseVec(heading);
					if (heading.Y > CeAiCos(w->GetMaxDeflection()))
						be->StartLock(xUnit, w->GetLockTime(), TRUE);
				}
			}
		}
		PilotGameAccess::CameraSlot(idx) = savedCam;
		return;
	}
	if (idx == 1 && CeMultiBeAiHostIsSoloSkirmish())
	{
		CPlayer *p1 = GAME.GetPlayer(1);
		if (!p1 || p1->GetBattleEngine() != be)
			return;
		if (!p1->GetIsInFPV() || !GAME.GetCurrentCamera(1))
			p1->GotoFPView();
		if (!GAME.GetCurrentCamera(1))
			return;
		if (kind == CE_AI_CALL_FIRE) be->FireWeapon();
		else if (kind == CE_AI_CALL_CHARGE) be->ChargeWeapon();
		else be->HandleLocks();
	}
	else if (CeMultiBeAiHostIsOnlineMatch() && CeMultiBeAiHostSlotIsAi(idx) && idx < GAME.GetNPlayers())
	{
		CPlayer *pl = GAME.GetPlayer(idx);
		if (!pl || pl->GetBattleEngine() != be)
			return;
		if (!pl->GetIsInFPV() || !GAME.GetCurrentCamera(idx))
			pl->GotoFPView();
		if (!GAME.GetCurrentCamera(idx))
			return;
		if (kind == CE_AI_CALL_FIRE) be->FireWeapon();
		else if (kind == CE_AI_CALL_CHARGE) be->ChargeWeapon();
		else be->HandleLocks();
	}
}

void SyncStoreTracker(CeMultiBeSlotBrain &brain, CBattleEngine *be, bool healedThisTick)
{
	CBattleEngineData *cfg = be ? be->GetConfiguration() : NULL;
	if (!cfg)
		return;

	if (!brain.storeTrackerInit)
	{
		for (int s = 0; s < kBattleEngineStores; ++s)
		{
			brain.storeVal[s] = cfg->mStoreHeat[s] ? 0.0f : cfg->mStoreValue[s];
			brain.storeOverheat[s] = false;
		}
		brain.storeTrackerInit = true;
	}
	else
	{
		for (int s = 0; s < kBattleEngineStores; ++s)
		{
			if (cfg->mStoreHeat[s])
			{
				brain.storeVal[s] -= 1.0f;
				if (brain.storeVal[s] < 0.0f)
					brain.storeVal[s] = 0.0f;
				else if (brain.storeVal[s] < cfg->mStoreValue[s] * 0.75f)
					brain.storeOverheat[s] = false;
			}
			else if (healedThisTick)
			{
				brain.storeVal[s] = cfg->mStoreValue[s];
			}
		}
	}

	if (CWeapon *cur = be->GetCurrentWeapon())
	{
		const int store = cur->GetAmmoStore();
		if (store >= 0 && store < kBattleEngineStores)
		{
			if (be->IsEnergyWeapon())
			{
				brain.storeVal[store] = be->GetWeaponAmmoPercentage() * cfg->mStoreValue[store];
				brain.storeOverheat[store] = (be->IsWeaponOverheated() != FALSE);
			}
			else
			{
				brain.storeVal[store] = (float)be->GetWeaponAmmoCount();
				brain.storeOverheat[store] = false;
			}
		}
	}
}

void FillWeaponInfo(CeMultiBeWeaponInfo *out, int slotIndex, CWeapon *w, CBattleEngine *be,
                    const CeMultiBeSlotBrain &brain, CUnit *target, const FVector &tgtPos)
{
	memset(out, 0, sizeof(*out));
	out->slotIndex = slotIndex;
	if (!w || !be)
		return;
	CBattleEngineData *cfg = be->GetConfiguration();
	const int store = w->GetAmmoStore();
	const bool validStore = (cfg != NULL && store >= 0 && store < kBattleEngineStores);
	const bool isEnergy = validStore ? (cfg->mStoreHeat[store] != FALSE) : true;
	const float maxStore = (validStore && cfg->mStoreValue[store] > 0.01f) ? cfg->mStoreValue[store] : 100.0f;

	out->isActive = (w->IsActive() != FALSE);
	out->isEnergy = isEnergy;
	if (w == be->GetCurrentWeapon())
	{
		out->isOverheated = isEnergy ? (be->IsWeaponOverheated() != FALSE) : false;
		out->heatRatio = isEnergy ? CeAiClamp(be->GetWeaponAmmoPercentage(), 0.0f, 1.0f) : 0.0f;
		out->ammoCount = isEnergy ? 1 : (int)be->GetWeaponAmmoCount();
	}
	else if (validStore)
	{
		out->isOverheated = isEnergy ? brain.storeOverheat[store] : false;
		out->heatRatio = isEnergy ? CeAiClamp(brain.storeVal[store] / maxStore, 0.0f, 1.0f) : 0.0f;
		out->ammoCount = isEnergy ? 1 : (int)brain.storeVal[store];
	}
	else
	{
		out->ammoCount = 1;
	}

	out->minRange = w->GetMinRange(tgtPos);
	float maxR = w->GetActualMaxRange(tgtPos);
	if (!CeAiIsFinite(maxR) || maxR <= 5.0f)
		maxR = w->GetMaxRange(tgtPos);
	if (!CeAiIsFinite(maxR) || maxR <= 5.0f)
		maxR = 220.0f;
	out->maxRange = maxR;
	out->launchSpeed = w->GetLaunchVelocity();
	out->canCharge = (w->CanCharge() != FALSE);
	out->targetTypeOk = target ? (w->Unknown50a0d0(target->GetThingType()) != FALSE) : true;

	CWeaponModeData *curMode = w->GetCurrentMode();
	CWeaponModeData *mode = w->mMode ? w->mMode : curMode;
	if (mode)
	{
		out->power = mode->mPower;
		out->reloadTime = mode->mReloadTime;
		out->burstSize = mode->mBurstSize;
		out->volleySize = mode->mVolleySize;
		out->isHoming = (mode->mMaxLocks > 0);
		if (target && curMode != NULL && mode->mMaxLocks > 0)
			out->canLockTarget = (w->CanLock(target) != FALSE);
		if (CRoundData *rd = mode->mRound)
		{
			out->damage = rd->mUnknown1C;
			out->isBeam = (rd->mBeam != FALSE);
			out->isHoming = out->isHoming || (rd->mUnknown48 != FALSE);
			out->isMissile = (rd->mUnknown70 != FALSE);
			out->isExplosive = (rd->mExplode != FALSE) || (rd->mUnknown08 != NULL);
			out->isFlak = (rd->mFlak != FALSE);
			out->isTorpedo = (rd->mUnknown6C != FALSE);
			out->isBallistic = (rd->mGravity * 0.025f != 0.0f) && !out->isBeam && !out->isTorpedo;
		}
	}
}

void SelectBestAiWeapon(int idx, CBattleEngine *be, bool isJet, CUnit *target,
                        float targetDist, float targetAlt, bool targetInWater)
{
	if (!be || !target || be->IsFiring())
		return;
	CeMultiBeSlotBrain &brain = gAiBrain[idx];
	CBattleEngineWalkerPart *walker = isJet ? NULL : be->GetWalkerPart();
	CBattleEngineJetPart *jet = isJet ? be->GetJetPart() : NULL;
	const int total = isJet ? (jet ? jet->CountWeapons() : 0) : (walker ? walker->CountWeapons() : 0);
	if (total <= 1)
		return;

	CWeapon *curWep = isJet ? jet->GetCurrentWeapon() : walker->GetCurrentWeapon();
	if (!curWep)
		return;

	FVector tgtPos;
	target->GetTargetablePos(&tgtPos);
	if (!CeMultiBeIsFinitePos(tgtPos.X, tgtPos.Y, tgtPos.Z))
		tgtPos = target->GetPos();

	enum { kMaxAiWeapons = 12 };
	CeMultiBeWeaponInfo infos[kMaxAiWeapons];
	CWeapon *ptrs[kMaxAiWeapons];
	int count = 0;
	int curSlotIdx = 0;
	for (int i = 0; i < total && count < kMaxAiWeapons; ++i)
	{
		CWeapon *w = isJet ? jet->GetWeapon(i) : walker->GetWeapon(i);
		if (!w)
			continue;
		if (w == curWep)
			curSlotIdx = count;
		ptrs[count] = w;
		FillWeaponInfo(&infos[count], count, w, be, brain, target, tgtPos);
		count++;
	}
	if (count <= 1)
		return;

	CeMultiBeWeaponTargetCtx wCtx = {};
	wCtx.targetClass = ClassifyUnitTarget(target);
	wCtx.targetDist = targetDist;
	wCtx.targetAlt = targetAlt;
	wCtx.targetInWater = targetInWater;
	wCtx.isJetMode = isJet;

	const int pick = CeMultiBePickWeapon(infos, count, curSlotIdx, wCtx, brain.ticksSinceWeaponSwitch, 50, 14.0f);
	if (pick == curSlotIdx || pick < 0 || pick >= count)
		return;

	CWeapon *desired = ptrs[pick];
	for (int step = 0; step < total; ++step)
	{
		CWeapon *before = isJet ? jet->GetCurrentWeapon() : walker->GetCurrentWeapon();
		if (before == desired)
			break;
		if (isJet)
			jet->ChangeWeapon();
		else
			walker->ChangeWeapon();
		CWeapon *after = isJet ? jet->GetCurrentWeapon() : walker->GetCurrentWeapon();
		if (after == before || after == desired)
			break;
	}

	CWeapon *finalWep = isJet ? jet->GetCurrentWeapon() : walker->GetCurrentWeapon();
	if (finalWep != curWep)
	{
		brain.ticksSinceWeaponSwitch = 0;
		brain.heatPause = false;
		brain.chargeTicks = 0;
	}
	if (finalWep == desired)
	{
		CeMultiBeAiHostNoteWeaponPick();
	}
	else if (desired && !infos[pick].isEnergy)
	{
		const int store = desired->GetAmmoStore();
		if (store >= 0 && store < kBattleEngineStores)
			brain.storeVal[store] = 0.0f;
	}
}

void AbortAiWeaponCharge(CBattleEngine *be)
{
	if (!be)
		return;
	be->LoseWeaponCharge();
	be->CeClearSlowMovement();
}

void ExecuteAiFireDecision(int idx, CBattleEngine *be, CWeapon *curWep, CeMultiBeSlotBrain &brain,
                           const CeMultiBeFireGateInput &fireIn, CePilotInput &pilotOut)
{
	const CeMultiBeFireDecision dec = CeMultiBeEvaluateFireGate(fireIn);
	brain.heatPause = dec.nextHeatPause;

	if (dec.action == CE_AI_FIRE_SHOOT)
	{
		pilotOut.fire = 1u;
		brain.chargeTicks = 0;
	}
	else if (dec.action == CE_AI_FIRE_CHARGE)
	{
		pilotOut.charge = 1u;
		brain.chargeTicks++;
		if (brain.chargeTicks >= 36 && curWep && curWep->ReadyToFire())
		{
			pilotOut.charge = 0u;
			pilotOut.fire = 1u;
			brain.chargeTicks = 0;
		}
	}
	else if (brain.chargeTicks > 0)
	{
		pilotOut.abortCharge = 1u;
		brain.chargeTicks = 0;
	}
	(void)idx;
	(void)be;
}

} // namespace

void CeAiPilotL1Reset(int slot, float initialLife, uint32_t matchSeed, bool onlineSeeded)
{
	if (slot < 0)
	{
		for (int i = 0; i < CE_MULTIBE_MAX; ++i)
			gAiBrain[i].Reset(i);
		return;
	}
	if (slot >= CE_MULTIBE_MAX)
		return;
	gAiBrain[slot].Reset(slot);
	if (onlineSeeded)
	{
		gAiBrain[slot].breakTurnDir = (CePlayerAiSeed(matchSeed, static_cast<uint8_t>(slot), 0u) & 1u) ? 1 : -1;
		gAiBrain[slot].strafeDir = (CePlayerAiSeed(matchSeed, static_cast<uint8_t>(slot), 1u) & 1u) ? 1 : -1;
	}
	if (initialLife > 0.0f)
		gAiBrain[slot].damage.Reset(initialLife);
}

void CePilotApplySlot(int slot, const CePilotInput &in, uint32_t activeToggles)
{
	if (slot < 0 || slot >= CE_MULTIBE_MAX)
		return;
	CBattleEngine *be = CeMultiBeAiHostSlotEngine(slot);
	if (!be || !UnitExistsInWorld(be) || be->IsDying())
		return;

	const EBattleEngineState state = be->GetState();
	if (state == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || state == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
		return;

	if (in.morph && (activeToggles & CE_TOGGLE_FLIGHT))
	{
		const bool wasJet = (state == BATTLE_ENGINE_STATE_JET);
		const float groundZ = MAP.Collide(be->GetPos());
		const float alt = CeAiIsFinite(groundZ) ? (groundZ - be->GetPos().Z) : 0.0f;
		be->Morph();
		if (be->GetState() != state)
		{
			CeMultiBeAiHostNoteMorph(wasJet, alt);
			gAiBrain[slot].ticksSinceMorph = 0;
			gAiBrain[slot].dash.Reset();
			gAiBrain[slot].chargeTicks = 0;
			gAiBrain[slot].jetPhase = CE_AI_JET_CRUISE;
			gAiBrain[slot].jetPhaseTicks = 0;
			gAiBrain[slot].prevState = be->GetState();
			return;
		}
		gAiBrain[slot].ticksSinceMorph = 0;
	}

	const bool isJet = (be->GetState() == BATTLE_ENGINE_STATE_JET);
	if (isJet)
	{
		CBattleEngineJetPart *jet = be->GetJetPart();
		if (!jet)
			return;
		jet->Turn(in.yawRate);
		jet->Pitch(in.pitchRate);
		jet->Thrust(in.thrustVy);
	}
	else
	{
		CBattleEngineWalkerPart *walker = be->GetWalkerPart();
		if (!walker)
			return;
		if (in.landingJets)
			walker->ActivateLandingJets();
		if (in.yawRate != 0.0f)
			walker->Rotate(in.yawRate);
		if (in.pitchRate != 0.0f)
			walker->Pitch(in.pitchRate);
		if (in.resetStrafeAxis)
			walker->StrafeRight(0.0f);
		if (in.forward < -0.01f)
			walker->Forward(in.forward);
		else if (in.forward > 0.01f)
			walker->Backward(in.forward);
		if (in.strafe < -0.01f)
			walker->StrafeLeft(in.strafe);
		else if (in.strafe > 0.01f)
			walker->StrafeRight(in.strafe);
	}

	if (in.updateLocks)
		InvokeAiWeaponCall(slot, be, CE_AI_CALL_LOCKS);
	if (in.fire)
		InvokeAiWeaponCall(slot, be, CE_AI_CALL_FIRE);
	else if (in.charge)
		InvokeAiWeaponCall(slot, be, CE_AI_CALL_CHARGE);
	else if (in.abortCharge)
		AbortAiWeaponCharge(be);
}

void CeAiPilotL1TickSlot(int idx, const CePlayerObs &obs, const CeStrategicPrior *prior)
{
	if (idx < 0 || idx >= CE_MULTIBE_MAX)
		return;
	CBattleEngine *be = CeMultiBeAiHostSlotEngine(idx);
	if (!be || !UnitExistsInWorld(be) || be->IsDying())
		return;

	CeMultiBeSlotBrain &brain = gAiBrain[idx];
	brain.ticksSinceMorph++;
	brain.ticksSinceWeaponSwitch++;

	const EBattleEngineState state = be->GetState();
	if (state != brain.prevState)
	{
		if (state == BATTLE_ENGINE_STATE_JET)
			brain.ticksSinceJetEnter = 0;
		if ((brain.prevState == BATTLE_ENGINE_STATE_JET ||
		     brain.prevState == BATTLE_ENGINE_STATE_MORPHING_INTO_JET ||
		     brain.prevState == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER) &&
		    state == BATTLE_ENGINE_STATE_WALKER)
		{
			brain.ticksSinceMorph = 0;
			brain.jetPhase = CE_AI_JET_CRUISE;
			brain.jetPhaseTicks = 0;
			brain.dash.Reset();
			brain.chargeTicks = 0;
		}
		brain.prevState = state;
	}
	if (state == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || state == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER)
		return;
	if (state == BATTLE_ENGINE_STATE_JET)
		brain.ticksSinceJetEnter++;

	const float now = EVENT_MANAGER.GetTime();
	const float prevLife = brain.damage.lastLife;
	const float curLife = be->GetLife();
	const float maxLife = be->GetMaxLife();
	const float lifeRatio = (maxLife > 0.01f) ? CeMultiBeClamp(curLife / maxLife, 0.0f, 1.0f) : 1.0f;
	const bool healedThisTick = (prevLife > 0.0f && curLife > prevLife + 0.02f);
	brain.damage.Update(curLife, maxLife, be->GetLastDamageTime(), now);

	const float waterZ = MAP.GetWaterLevel();
	const float waterMargin = 0.9f;
	const FVector myPos = be->GetPos();
	const float groundZ = MAP.Collide(myPos);
	const bool groundIsDry = CeMultiBeIsAboveWater(groundZ, waterZ, 1.15f);
	const bool nearWater = !CeMultiBeIsAboveWater(groundZ, waterZ, waterMargin) ||
	                       !CeMultiBeIsAboveWater(myPos.Z, waterZ, waterMargin);
	const bool isJet = (state == BATTLE_ENGINE_STATE_JET);
	const bool isOnGround = (be->IsOnGround() != FALSE) || (be->IsOnObject() != FALSE);
	const float altitudeAboveGround = CeAiIsFinite(groundZ) ? (groundZ - myPos.Z) : 0.0f;

	const FMatrix bodyOri = be->GetOrientation();
	FVector bodyFwd = bodyOri * FVector(0.0f, 1.0f, 0.0f);
	bodyFwd.Z = 0.0f;
	if (bodyFwd.MagnitudeSq() > 1e-4f)
		CeAiNormaliseVec(bodyFwd);
	else
		bodyFwd = FVector(0.0f, 1.0f, 0.0f);
	const bool aheadGroundIsDry = IsDryAndInBounds(myPos + bodyFwd * 10.0f, waterZ, 1.0f);

	const float maxEnergy = be->GetMaxEnergy();
	const float energyRatio = (maxEnergy > 0.01f) ? CeMultiBeClamp(be->GetEnergy() / maxEnergy, 0.0f, 1.0f) : 1.0f;

	const CeMultiBeTacticalScan scan = GatherTacticalContext(be, idx, now, prior);
	const bool rearmRefresh = healedThisTick || !be->NeedsRearm() ||
	                          (scan.hasRepairPad && scan.repairPadDist < 4.5f);
	SyncStoreTracker(brain, be, rearmRefresh);
	CUnit *target = scan.target;
	brain.lastTarget = target;

	CeMultiBeSurvivalInput survIn = {};
	survIn.lifeRatio = lifeRatio;
	survIn.energyRatio = energyRatio;
	survIn.isJet = isJet;
	survIn.groundIsDry = groundIsDry;
	survIn.hasRepairPad = scan.hasRepairPad;
	survIn.hasFriendlyBase = scan.hasFriendlyBase;
	survIn.currentlyRetreating = brain.retreating || (prior && prior->posture == CE_STRAT_REPAIR && lifeRatio < 0.60f);
	const CeMultiBeSurvivalDecision surv = CeMultiBeEvaluateSurvival(survIn);

	if ((surv.retreating && !brain.retreating) || (surv.seekDryShore && !brain.seekDryShore))
		CeMultiBeAiHostNoteRetreat();
	brain.retreating = surv.retreating;
	brain.seekDryShore = surv.seekDryShore;

	const bool outOfBounds = (myPos.X < 36.0f || myPos.X > 476.0f || myPos.Y < 36.0f || myPos.Y > 476.0f);

	float startArr[3] = { myPos.X, myPos.Y, myPos.Z };
	CeMultiBeAiHostSlotStartPos(idx, startArr);
	const FVector startPos(startArr[0], startArr[1], startArr[2]);

	FVector goalPos = startPos;
	bool goalIsRepairPad = false;
	if (outOfBounds)
	{
		goalPos = FVector(256.0f, 256.0f, groundZ - 22.0f);
	}
	else if (surv.seekDryShore)
	{
		if (scan.hasRepairPad && IsDryAndInBounds(scan.repairPadPos, waterZ, 1.0f))
			goalPos = scan.repairPadPos;
		else if (scan.hasFriendlyBase && IsDryAndInBounds(scan.friendlyBasePos, waterZ, 1.0f))
			goalPos = scan.friendlyBasePos;
		else
			goalPos = startPos;
	}
	else if (surv.retreating)
	{
		if (scan.hasRepairPad)
		{
			goalPos = scan.repairPadPos;
			goalIsRepairPad = true;
		}
		else if (scan.hasFriendlyBase)
		{
			goalPos = scan.friendlyBasePos;
		}
	}
	else if (target)
	{
		goalPos = target->GetPos();
	}
	else
	{
		FVector battleLineGoal;
		if (!CeMultiBeAiHostIsPanoramicCam() && FindBattleLineGoal(be, myPos, &battleLineGoal))
			goalPos = battleLineGoal;
		else if (scan.nearestHostileAnywhere)
			goalPos = scan.nearestHostileAnywhere->GetPos();
		else
			goalPos = startPos;
	}

	const FVector toGoal = goalPos - myPos;
	const float goalHorizDist = CeAiSqrt(toGoal.X * toGoal.X + toGoal.Y * toGoal.Y);

	FVector tgtPos = goalPos;
	float targetHorizDist = goalHorizDist;
	float targetDist3D = CeAiSqrt(toGoal.MagnitudeSq());
	float targetAltAboveGround = 0.0f;
	bool targetInWater = false;
	if (target)
	{
		target->GetTargetablePos(&tgtPos);
		if (!CeMultiBeIsFinitePos(tgtPos.X, tgtPos.Y, tgtPos.Z))
			tgtPos = target->GetPos();
		const FVector toTgt = tgtPos - myPos;
		targetHorizDist = CeAiSqrt(toTgt.X * toTgt.X + toTgt.Y * toTgt.Y);
		targetDist3D = CeAiSqrt(toTgt.MagnitudeSq());
		const float tgtGroundZ = MAP.Collide(tgtPos);
		const float tgtSurfaceZ = (tgtGroundZ < waterZ) ? tgtGroundZ : waterZ;
		targetAltAboveGround = tgtSurfaceZ - tgtPos.Z;
		targetInWater = (tgtGroundZ >= waterZ - 0.1f) && (tgtPos.Z >= waterZ - 1.5f);
	}

	// Evaluate deterministic pure-integer INT8 Layer-1 pilot MLP (zero libm, < 0.1 ms/tick)
	// to apply bounded micro-trims strictly inside ce_multibe_ai.h safety guardrails.
	int16_t mlpFeaturesQ12[kCeAiMlpInDim];
	int16_t mlpOutQ12[kCeAiMlpOutDim];
	CeAiPilotExtractFeaturesQ12(obs, prior, mlpFeaturesQ12);
	CeAiPilotMlpInfer(mlpFeaturesQ12, mlpOutQ12);
	const float mlpStrafeTrim    = static_cast<float>(mlpOutQ12[CE_AI_OUT_STRAFE_TRIM]) * (0.06f / 4096.0f);
	const float mlpFwdTrim       = static_cast<float>(mlpOutQ12[CE_AI_OUT_FWD_TRIM]) * (0.05f / 4096.0f);
	const float mlpJetTurnTrim   = static_cast<float>(mlpOutQ12[CE_AI_OUT_JET_TURN_TRIM]) * (0.05f / 4096.0f);
	const float mlpJetThrustTrim = static_cast<float>(mlpOutQ12[CE_AI_OUT_JET_THRUST_TRIM]) * (0.04f / 4096.0f);
	const float mlpCruiseBias    = static_cast<float>(mlpOutQ12[CE_AI_OUT_JET_CRUISE_BIAS]) * (2.0f / 4096.0f);

	const int simTicks = CeMultiBeAiHostSimTicks();
	CeMultiBeSmartMorphInput morphIn = {};
	morphIn.isJet = isJet;
	morphIn.flightAllowed = (obs.activeToggles & CE_TOGGLE_FLIGHT) != 0u;
	morphIn.groundIsDry = groundIsDry;
	morphIn.nearWaterHazard = nearWater;
	morphIn.energyRatio = energyRatio;
	morphIn.hasHostile = (target != NULL);
	morphIn.targetDist = target ? targetHorizDist : -1.0f;
	morphIn.targetAltitude = targetAltAboveGround;
	morphIn.goalDist = goalHorizDist;
	morphIn.retreating = surv.retreating;
	morphIn.nearRepairPad = goalIsRepairPad && (goalHorizDist < 24.0f);
	morphIn.baseDefenseUrgent = scan.baseDefenseUrgent;
	morphIn.ticksSinceMorph = brain.ticksSinceMorph;
	morphIn.slotIndex = idx;
	morphIn.simTicks = simTicks;
	morphIn.altitudeAboveGround = altitudeAboveGround;
	morphIn.isOnGround = isOnGround;
	morphIn.aheadGroundIsDry = aheadGroundIsDry;

	CePilotInput pilot = {};
	pilot.desiredWeaponSlot = -1;
	if (CeMultiBeShouldMorphSmart(morphIn))
	{
		pilot.morph = 1u;
		CePilotApplySlot(idx, pilot, obs.activeToggles);
		if (be->GetState() != state)
			return;
		pilot.morph = 0u;
	}

	const bool walkerAirborne = (!isJet && !isOnGround && altitudeAboveGround > 4.0f);

	if (target && !outOfBounds && !walkerAirborne)
		SelectBestAiWeapon(idx, be, isJet, target, targetDist3D, targetAltAboveGround, targetInWater);

	CWeapon *curWep = be->GetCurrentWeapon();

	FVector aimPoint = tgtPos;
	if (target)
	{
		const FVector tVelTick = target->GetVelocity();
		const CeAiVec3 myV = { myPos.X, myPos.Y, myPos.Z };
		const CeAiVec3 tgtV = { tgtPos.X, tgtPos.Y, tgtPos.Z };
		const CeAiVec3 velMps = { tVelTick.X * 20.0f, tVelTick.Y * 20.0f, tVelTick.Z * 20.0f };
		const float projSpeed = curWep ? curWep->GetLaunchVelocity() : 0.0f;
		const CeAiVec3 lead = CeMultiBeComputeLeadPoint(myV, tgtV, velMps, projSpeed, 2.2f);
		aimPoint = FVector(lead.x, lead.y, lead.z);
	}

	const bool isLockWeapon = (curWep != NULL && curWep->GetMaxLocks() > 0);
	if (isLockWeapon && target && !outOfBounds && !walkerAirborne)
		pilot.updateLocks = 1u;

	const float wepMinRange = (curWep && target) ? curWep->GetMinRange(tgtPos) : 0.0f;
	float wepMaxRange = (curWep && target) ? curWep->GetActualMaxRange(tgtPos) : 220.0f;
	if (!CeAiIsFinite(wepMaxRange) || wepMaxRange <= 5.0f)
		wepMaxRange = (curWep && target) ? curWep->GetMaxRange(tgtPos) : 220.0f;

	CeMultiBeFireGateInput fireIn = {};
	fireIn.hasHostile = (target != NULL && !outOfBounds && !walkerAirborne &&
	                     !(goalIsRepairPad && surv.retreating && goalHorizDist < 12.0f));
	fireIn.isJet = isJet;
	fireIn.weaponActive = (curWep != NULL && curWep->IsActive() != FALSE);
	fireIn.readyToFire = (curWep != NULL && curWep->ReadyToFire() != FALSE);
	fireIn.readyToCharge = (curWep != NULL && curWep->ReadyToCharge() != FALSE);
	fireIn.canCharge = (curWep != NULL && curWep->CanCharge() != FALSE);
	fireIn.fullyCharged = (curWep != NULL && curWep->FullyCharged() != FALSE);
	fireIn.chargeRatio = curWep ? CeAiClamp(curWep->GetCharge(), 0.0f, 1.0f) : 0.0f;
	fireIn.isEnergyWeapon = (be->IsEnergyWeapon() != FALSE);
	fireIn.isOverheated = (be->IsWeaponOverheated() != FALSE);
	fireIn.heatOrAmmoRatio = CeAiClamp(be->GetWeaponAmmoPercentage(), 0.0f, 1.0f);
	fireIn.ammoCount = (int)be->GetWeaponAmmoCount();
	fireIn.isLockWeapon = isLockWeapon;
	fireIn.activeLocks = isLockWeapon ? be->CountLocks() : 0;
	fireIn.locksFinished = isLockWeapon ? (be->LocksFinished() != FALSE) : false;
	fireIn.targetDist = targetDist3D;
	fireIn.minRange = wepMinRange;
	fireIn.maxRange = wepMaxRange;
	fireIn.heatCoolingPause = brain.heatPause;

	if (isJet)
	{
		const CEulerAngles euler = be->GetCurrentEulerOrientation();
		const FVector aheadPos = myPos + bodyFwd * 28.0f;
		const float aheadGroundZ = MAP.Collide(aheadPos);

		const bool wantLanding = CeMultiBeWantsJetLanding(
			true, (obs.activeToggles & CE_TOGGLE_FLIGHT) != 0u, groundIsDry, aheadGroundIsDry,
			energyRatio, surv.retreating, goalIsRepairPad && (goalHorizDist < 55.0f),
			target != NULL, target ? targetHorizDist : -1.0f, targetAltAboveGround,
			brain.ticksSinceMorph);
		const FVector jetGoal = (!wantLanding && target && !surv.retreating && !surv.seekDryShore && !outOfBounds) ? aimPoint : goalPos;
		const FVector toJetGoal = jetGoal - myPos;

		CeMultiBeJetFlightInput jIn = {};
		jIn.curYaw = euler.mYaw;
		jIn.curPitch = euler.mPitch;
		jIn.posZ = myPos.Z;
		jIn.groundZ = groundZ;
		jIn.aheadGroundZ = aheadGroundZ;
		jIn.waterZ = waterZ;
		jIn.toGoal.x = toJetGoal.X;
		jIn.toGoal.y = toJetGoal.Y;
		jIn.toGoal.z = toJetGoal.Z;
		jIn.hasHostile = (target != NULL && !outOfBounds);
		jIn.isRetreating = (surv.retreating || surv.seekDryShore);
		jIn.underHeavyFire = brain.damage.heavyHitPending;
		jIn.outOfBounds = outOfBounds;
		jIn.desiredCruiseHeight = CeAiClamp(22.0f + mlpCruiseBias, 18.0f, 26.0f);
		jIn.prevPhase = brain.jetPhase;
		jIn.phaseTicks = brain.jetPhaseTicks;
		jIn.breakTurnDir = brain.breakTurnDir;
		jIn.wantLanding = wantLanding;
		jIn.groundIsDry = groundIsDry;
		jIn.aheadGroundIsDry = aheadGroundIsDry;
		jIn.postTakeoffClimb = (brain.ticksSinceJetEnter <= 16);

		const CeMultiBeJetFlightCmd jCmd = CeMultiBeComputeJetFlight(jIn);
		brain.jetPhase = jCmd.nextPhase;
		brain.jetPhaseTicks = jCmd.nextPhaseTicks;
		brain.breakTurnDir = jCmd.nextBreakTurnDir;
		if (jCmd.startedBreakoff)
		{
			brain.damage.heavyHitPending = false;
			CeMultiBeAiHostNoteEvade();
		}

		const bool jetTrimSafe = !jCmd.emergencyPullUp && !jCmd.inLanding && !outOfBounds;
		pilot.yawRate = jetTrimSafe ? CeAiClamp(jCmd.turnInput + mlpJetTurnTrim, -0.95f, 0.95f) : jCmd.turnInput;
		pilot.pitchRate = jCmd.pitchInput;
		pilot.thrustVy = jetTrimSafe ? CeAiClamp(jCmd.thrustVy + mlpJetThrustTrim, -0.84f, 0.52f) : jCmd.thrustVy;

		fireIn.emergencyPullUp = jCmd.emergencyPullUp || jCmd.inLanding || (jCmd.nextPhase == CE_AI_JET_BREAKOFF);
		fireIn.yawErrAbs = jCmd.yawErrAbs;
		fireIn.pitchErrAbs = jCmd.pitchErrAbs;
		fireIn.maxYawCone = 0.30f;
		fireIn.maxPitchCone = 0.34f;
		ExecuteAiFireDecision(idx, be, curWep, brain, fireIn, pilot);
		CePilotApplySlot(idx, pilot, obs.activeToggles);
		return;
	}

	CBattleEngineWalkerPart *walker = be->GetWalkerPart();
	if (!walker)
		return;

	if (!isOnGround && altitudeAboveGround <= 6.5f && be->GetVelocity().Z > 0.28f)
		pilot.landingJets = 1u;

	const CEulerAngles curEuler = be->GetCurrentEulerOrientation();
	const bool moveOnlyGoal = (outOfBounds || nearWater || walkerAirborne || (surv.retreating && goalIsRepairPad));

	FVector lookDelta = (target && !moveOnlyGoal) ? (aimPoint - myPos) : toGoal;
	const float lookHoriz = CeAiSqrt(lookDelta.X * lookDelta.X + lookDelta.Y * lookDelta.Y);
	if (lookHoriz > 1.0f)
	{
		const float desiredYaw = CeAiAzimuth(lookDelta.X, lookDelta.Y);
		const float yawDiff = CeAiWrapAngle(desiredYaw - curEuler.mYaw);
		if (CeAiAbs(yawDiff) > 0.025f)
			pilot.yawRate = CeAiClamp(-yawDiff * 1.45f, -0.95f, 0.95f);
		const float desiredPitch = (target && !moveOnlyGoal) ? CeAiElevation(lookDelta.X, lookDelta.Y, lookDelta.Z) : 0.0f;
		pilot.pitchRate = CeMultiBeComputeWalkerPitchInput(curEuler.mPitch, desiredPitch);
	}
	else
	{
		if (CeMultiBeAiHostIsOnlineMatch())
		{
			const uint32_t s = CePlayerAiSeed(obs.matchSeed, static_cast<uint8_t>(idx), static_cast<uint32_t>(simTicks / 45));
			pilot.yawRate = (s & 1u) ? 0.16f : -0.16f;
		}
		else
		{
			pilot.yawRate = (((simTicks / 45 + idx) & 1) ? 0.16f : -0.16f);
		}
	}

	const FMatrix ori = be->GetOrientation();
	FVector fwd = ori * FVector(0.0f, 1.0f, 0.0f);
	FVector right = ori * FVector(1.0f, 0.0f, 0.0f);
	fwd.Z = 0.0f;
	right.Z = 0.0f;
	if (fwd.MagnitudeSq() > 1e-4f) CeAiNormaliseVec(fwd); else fwd = FVector(0.0f, 1.0f, 0.0f);
	if (right.MagnitudeSq() > 1e-4f) CeAiNormaliseVec(right); else right = FVector(1.0f, 0.0f, 0.0f);

	auto oobDistSq = [](const FVector &p) -> float {
		float ex = 0.0f, ey = 0.0f;
		if (p.X < 36.0f) ex = 36.0f - p.X;
		else if (p.X > 476.0f) ex = p.X - 476.0f;
		if (p.Y < 36.0f) ey = 36.0f - p.Y;
		else if (p.Y > 476.0f) ey = p.Y - 476.0f;
		return ex * ex + ey * ey;
	};
	const float myOobSq = outOfBounds ? oobDistSq(myPos) : 0.0f;
	auto probeDry = [&](const FVector &p, float margin) -> bool {
		if (IsDryAndInBounds(p, waterZ, margin))
			return true;
		if (!outOfBounds)
			return false;
		if (!CeMultiBeIsFinitePos(p.X, p.Y, p.Z))
			return false;
		if (p.X < 8.0f || p.X > 504.0f || p.Y < 8.0f || p.Y > 504.0f)
			return false;
		if (oobDistSq(p) >= myOobSq)
			return false;
		const float gz = MAP.Collide(p);
		return CeAiIsFinite(gz) && CeMultiBeIsAboveWater(gz, waterZ, margin);
	};

	const bool dryFwd = probeDry(myPos + fwd * 10.0f, waterMargin);
	const bool dryBack = probeDry(myPos - fwd * 10.0f, waterMargin);
	const bool dryLeft = probeDry(myPos - right * 10.0f, waterMargin);
	const bool dryRight = probeDry(myPos + right * 10.0f, waterMargin);
	const bool dryDashLeft = dryLeft && IsDryAndInBounds(myPos - right * 8.0f, waterZ, 1.15f) &&
	                         IsDryAndInBounds(myPos - right * 16.0f, waterZ, 1.15f);
	const bool dryDashRight = dryRight && IsDryAndInBounds(myPos + right * 8.0f, waterZ, 1.15f) &&
	                          IsDryAndInBounds(myPos + right * 16.0f, waterZ, 1.15f);

	const float yawToGoal = (goalHorizDist > 1.0f) ? CeAiAzimuth(toGoal.X, toGoal.Y) : curEuler.mYaw;
	const float yawErrToGoal = CeAiWrapAngle(yawToGoal - curEuler.mYaw);

	CeMultiBeWalkerMoveInput wIn = {};
	wIn.hasHostile = (target != NULL && !outOfBounds);
	wIn.isRetreating = surv.retreating;
	wIn.onRepairPad = goalIsRepairPad && (goalHorizDist < 2.8f);
	wIn.defensiveStandoff = surv.defensiveStandoff;
	wIn.nearWaterHazard = nearWater;
	wIn.outOfBounds = outOfBounds;
	wIn.targetDist = targetHorizDist;
	wIn.goalDist = goalHorizDist;
	wIn.yawErrToGoal = yawErrToGoal;
	wIn.underFire = (brain.damage.underFireTicks > 0);
	wIn.heavyHit = brain.damage.heavyHitPending;
	wIn.strafeDir = brain.strafeDir;
	wIn.strafeTicks = brain.strafeTicks;
	wIn.dryFwd = dryFwd;
	wIn.dryBack = dryBack;
	wIn.dryLeft = dryLeft;
	wIn.dryRight = dryRight;
	wIn.dryDashLeft = dryDashLeft;
	wIn.dryDashRight = dryDashRight;

	const CeMultiBeWalkerMoveCmd wCmd = CeMultiBeComputeWalkerMove(wIn);
	brain.strafeDir = wCmd.nextStrafeDir;
	brain.strafeTicks = wCmd.nextStrafeTicks;
	if (wCmd.reversedStrafe)
		CeMultiBeAiHostNoteEvade();

	const bool dashDrySafe = (wCmd.dashDir < 0) ? dryDashLeft : dryDashRight;
	const bool armedDrySafe = (brain.dash.dashDir < 0) ? dryDashLeft : dryDashRight;
	const CeAiDashPhase prevDashPhase = brain.dash.phase;
	const CeMultiBeWalkerDashOutput dOut = CeMultiBeStepWalkerDash(
		brain.dash, wCmd.wantDash && brain.chargeTicks == 0, wCmd.dashDir,
		be->IsOnGround() != FALSE, walker->GetDashCount(),
		(brain.dash.phase == CE_AI_DASH_ARMED) ? armedDrySafe : dashDrySafe, now);

	if (dOut.overrideStrafe)
	{
		if (prevDashPhase == CE_AI_DASH_IDLE && brain.dash.phase == CE_AI_DASH_ARMED)
			pilot.resetStrafeAxis = 1u;
		pilot.strafe = dOut.strafeValue;
		if (dOut.dashTriggered)
		{
			brain.damage.heavyHitPending = false;
			CeMultiBeAiHostNoteEvade();
			if (walker->GetDashCount() > 0)
				CeMultiBeAiHostNoteDash();
		}
	}
	else
	{
		if (wCmd.wantDash)
			brain.damage.heavyHitPending = false;
		if (wCmd.forwardInput < -0.01f)
			pilot.forward = wCmd.forwardInput;
		else if (wCmd.backwardInput > 0.01f)
			pilot.forward = wCmd.backwardInput;
		pilot.strafe = wCmd.strafeInput;

		// Apply bounded MLP combat trims strictly within dry-ground & non-hazard guardrails.
		if (target && !surv.retreating && !nearWater && !outOfBounds && !wIn.onRepairPad)
		{
			const float candFwd = CeAiClamp(pilot.forward + mlpFwdTrim, -0.82f, 0.82f);
			if ((candFwd < 0.0f && dryFwd) || (candFwd > 0.0f && dryBack))
				pilot.forward = candFwd;
			const float candStrafe = CeAiClamp(pilot.strafe + mlpStrafeTrim, -0.82f, 0.82f);
			if ((candStrafe < 0.0f && dryLeft) || (candStrafe > 0.0f && dryRight))
				pilot.strafe = candStrafe;
		}
	}

	const FVector toAim = aimPoint - myPos;
	const float aimYawErr = target ? CeAiAbs(CeAiWrapAngle(CeAiAzimuth(toAim.X, toAim.Y) - curEuler.mYaw)) : 3.14159f;
	const float aimPitchErr = target ? CeAiAbs(CeAiWrapAngle(CeAiElevation(toAim.X, toAim.Y, toAim.Z) - curEuler.mPitch)) : 3.14159f;
	fireIn.emergencyPullUp = false;
	fireIn.yawErrAbs = aimYawErr;
	fireIn.pitchErrAbs = aimPitchErr;
	fireIn.maxYawCone = 0.25f;
	fireIn.maxPitchCone = 0.28f;
	ExecuteAiFireDecision(idx, be, curWep, brain, fireIn, pilot);
	CePilotApplySlot(idx, pilot, obs.activeToggles);
}

#else

void CeAiPilotL1Reset(int, float, uint32_t, bool) {}
void CePilotApplySlot(int, const CePilotInput &, uint32_t) {}
void CeAiPilotL1TickSlot(int, const CePlayerObs &, const CeStrategicPrior *) {}

#endif
