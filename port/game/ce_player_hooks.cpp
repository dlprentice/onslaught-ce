// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_player_hooks.cpp: Network-neutral SIM observation builder & AI action dispatcher.
// Captures read-only CePlayerObs into static BSS storage (zero stack bloat, zero GetCurrentWeapon()
// mutation, zero intrusive iterator mutation, zero CeEntityHandleFor() calls), sorted strictly by
// (distSqQuantized, matchLocalSpawnSeq), and dispatches Layer-1 pilot & toggle-gated Layer-2 AI.
#include "ce_player_hooks.h"
#include "ce_policy.h"
#include "ce_commander.h"
#include "ce_playunit_slots.h"
#include "ai/ce_ai_pilot_l1.h"
#include "ai/ce_ai_tactical_l2.h"
#include "ai/ce_ai_strategist_l3.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "BattleEngine.h"
#include "BattleEngineWalkerPart.h"
#include "BattleEngineJetPart.h"
#include "unit.h"
#include "weapon.h"
#include "ce_multibe.h"
#include "ce_multibe_ai.h"

#include <string.h>

extern bool PortCeLayer();

namespace CePlayerHooksAccess
{

// Standard C++ explicit-instantiation read-only accessors for private CBattleEngine weapon store arrays
// so BuildSlotObs reads live heat, overheat, and ammo counts without touching BattleEngine.h or calling
// CBattleEngine::GetCurrentWeapon() (which mutates mCurrentWeapon / intrusive weapon iterators).
template <typename Tag>
struct CeBeStoreSlot
{
	static typename Tag::MemberPtr ptr;
};
template <typename Tag>
typename Tag::MemberPtr CeBeStoreSlot<Tag>::ptr = nullptr;

template <typename Tag, typename Tag::MemberPtr P>
struct CeBeStoreBind
{
	struct Init
	{
		Init() { CeBeStoreSlot<Tag>::ptr = P; }
	};
	static Init sInit;
};
template <typename Tag, typename Tag::MemberPtr P>
typename CeBeStoreBind<Tag, P>::Init CeBeStoreBind<Tag, P>::sInit;

struct BeStoreValueTag
{
	typedef float (CBattleEngine::*MemberPtr)[kBattleEngineStores];
};
struct BeStoreOverheatTag
{
	typedef BOOL (CBattleEngine::*MemberPtr)[kBattleEngineStores];
};
struct BeStoreHeatTag
{
	typedef BOOL (CBattleEngine::*MemberPtr)[kBattleEngineStores];
};

template struct CeBeStoreBind<BeStoreValueTag, &CBattleEngine::mStoreValue>;
template struct CeBeStoreBind<BeStoreOverheatTag, &CBattleEngine::mStoreOverheat>;
template struct CeBeStoreBind<BeStoreHeatTag, &CBattleEngine::mStoreHeat>;

} // namespace CePlayerHooksAccess

namespace
{

// Static BSS host storage (~34 KB total for 4 slots; never allocated on 32-bit x86 stack).
CePlayerObs          gSlotObs[CE_PLAYER_MAX_SLOTS];
bool                 gSlotObsValid[CE_PLAYER_MAX_SLOTS];
CeAiTacticalL2State  gTacticalState[CE_PLAYER_MAX_SLOTS];
CePlayerObsSinkFn    gObsSink = nullptr;
uint32_t             gLastActTick = 0u;

// Strictly read-only prefix views matching BattleEngineWalkerPart.h:88-91 and BattleEngineJetPart.h:91-92.
// Reading mCurrentWeapon directly avoids CBattleEngine::GetCurrentWeapon() mutating out-of-range indices
// and walking mWeapons via stack ListIterator<CWeapon> avoids mutating SPtrSet's intrusive mIterator.
struct CeWalkerPartPrefix
{
	SPtrSet<CWeapon>	mWeapons;
	int					mCurrentWeapon;
	BOOL				mShieldsRecharging;
	CWeapon				*mPrimaryWeapon;
	CWeapon				*mAugWeapon;
};

struct CeJetPartPrefix
{
	SPtrSet<CWeapon>	mWeapons;
	int					mCurrentWeapon;
};

static_assert(sizeof(CeWalkerPartPrefix) <= sizeof(CBattleEngineWalkerPart), "walker prefix fits class");
static_assert(sizeof(CeJetPartPrefix) <= sizeof(CBattleEngineJetPart), "jet prefix fits class");

CWeapon *ReadCurrentWeaponReadOnly(CBattleEngine *be, bool isJet, int *outIndex, int *outCount)
{
	*outIndex = -1;
	*outCount = 0;
	if (!be) return nullptr;
	if (isJet)
	{
		CBattleEngineJetPart *jet = be->GetJetPart();
		if (!jet) return nullptr;
		const auto *view = reinterpret_cast<const CeJetPartPrefix *>(jet);
		const int curIdx = view->mCurrentWeapon;
		*outIndex = curIdx;
		SPtrSet<CWeapon> *setPtr = const_cast<SPtrSet<CWeapon> *>(&view->mWeapons);
		ListIterator<CWeapon> it(setPtr);
		int n = 0;
		CWeapon *found = nullptr;
		for (CWeapon *w = it.First(); w; w = it.Next(), ++n)
		{
			if (n == curIdx)
				found = w;
		}
		*outCount = n;
		return found;
	}
	CBattleEngineWalkerPart *walker = be->GetWalkerPart();
	if (!walker) return nullptr;
	const auto *view = reinterpret_cast<const CeWalkerPartPrefix *>(walker);
	const int curIdx = view->mCurrentWeapon;
	*outIndex = curIdx;
	int n = 0;
	CWeapon *found = nullptr;
	if (view->mPrimaryWeapon)
	{
		if (curIdx == 0)
			found = (be->IsAugActive() && view->mAugWeapon) ? view->mAugWeapon : view->mPrimaryWeapon;
		++n;
	}
	SPtrSet<CWeapon> *setPtr = const_cast<SPtrSet<CWeapon> *>(&view->mWeapons);
	ListIterator<CWeapon> it(setPtr);
	for (CWeapon *w = it.First(); w; w = it.Next(), ++n)
	{
		if (n == curIdx && !found)
			found = w;
	}
	*outCount = n;
	return found;
}

bool ContainsAsciiICase(const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle) return false;
	for (const char *p = hay; *p; ++p)
	{
		const char *a = p, *b = needle;
		while (*a && *b)
		{
			char ca = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
			char cb = (*b >= 'A' && *b <= 'Z') ? char(*b + 32) : *b;
			if (ca != cb) break;
			++a; ++b;
		}
		if (!*b) return true;
	}
	return false;
}

uint8_t ClassifyTarget(CUnit *u)
{
	if (!u) return CE_AI_TGT_VEHICLE;
	if (u->IsA(THING_TYPE_BATTLE_ENGINE)) return CE_AI_TGT_BATTLE_ENGINE;
	if (u->IsA(THING_TYPE_AIR_UNIT)) return CE_AI_TGT_AIRCRAFT;
	if (u->IsA(THING_TYPE_BUILDING)) return CE_AI_TGT_BUILDING;
	if (u->IsA(THING_TYPE_EMPLACEMENT)) return CE_AI_TGT_TURRET;
	if (u->IsA(THING_TYPE_INFANTRY)) return CE_AI_TGT_INFANTRY;
	if (u->IsA(THING_TYPE_MECH)) return CE_AI_TGT_HEAVY_ARMOR;
	return CE_AI_TGT_VEHICLE;
}

bool DryProbe(const FVector &p, float waterZ, float margin)
{
	if (!CeMultiBeIsFinitePos(p.X, p.Y, p.Z)) return false;
	if (p.X < 36.0f || p.X > 476.0f || p.Y < 36.0f || p.Y > 476.0f) return false;
	return CeMultiBeIsAboveWater(MAP.Collide(p), waterZ, margin);
}

void BuildSlotObs(uint8_t slot, CBattleEngine *be, uint32_t simTick, uint32_t matchSeed,
                  uint32_t toggles, CePlayerObs &obs)
{
	memset(&obs, 0, sizeof(obs));
	obs.simTick = simTick;
	obs.matchSeed = matchSeed;
	obs.slot = slot;
	obs.activeToggles = toggles;
	if (!be) return;

	const EBattleEngineState st = be->GetState();
	const bool isJet = (st == BATTLE_ENGINE_STATE_JET);
	obs.isJet = isJet ? 1u : 0u;
	obs.isOnGround = ((be->IsOnGround() != FALSE) || (be->IsOnObject() != FALSE)) ? 1u : 0u;
	obs.isMorphing = (st == BATTLE_ENGINE_STATE_MORPHING_INTO_JET || st == BATTLE_ENGINE_STATE_MORPHING_INTO_WALKER) ? 1u : 0u;
	obs.allegiance = static_cast<int32_t>(be->GetAllegiance());

	const float maxLife = be->GetMaxLife();
	const float maxEnergy = be->GetMaxEnergy();
	obs.lifeRatio = (maxLife > 0.01f) ? CeAiClamp(be->GetLife() / maxLife, 0.0f, 1.0f) : 1.0f;
	obs.energyRatio = (maxEnergy > 0.01f) ? CeAiClamp(be->GetEnergy() / maxEnergy, 0.0f, 1.0f) : 1.0f;

	const FVector myPos = be->GetPos();
	const FVector myVel = be->GetVelocity();
	const CEulerAngles euler = be->GetCurrentEulerOrientation();
	const float waterZ = MAP.GetWaterLevel();
	const float groundZ = MAP.Collide(myPos);
	obs.pos[0] = myPos.X; obs.pos[1] = myPos.Y; obs.pos[2] = myPos.Z;
	obs.vel[0] = myVel.X; obs.vel[1] = myVel.Y; obs.vel[2] = myVel.Z;
	obs.yaw = euler.mYaw;
	obs.pitch = euler.mPitch;
	obs.groundZ = groundZ;
	obs.waterZ = waterZ;
	obs.altitude = CeAiIsFinite(groundZ) ? (groundZ - myPos.Z) : 0.0f;

	const FMatrix ori = be->GetOrientation();
	FVector fwd = ori * FVector(0.0f, 1.0f, 0.0f);
	FVector right = ori * FVector(1.0f, 0.0f, 0.0f);
	fwd.Z = 0.0f; right.Z = 0.0f;
	if (fwd.MagnitudeSq() > 1e-4f) fwd.Normalise(); else fwd = FVector(0.0f, 1.0f, 0.0f);
	if (right.MagnitudeSq() > 1e-4f) right.Normalise(); else right = FVector(1.0f, 0.0f, 0.0f);

	obs.aheadGroundZ = MAP.Collide(myPos + fwd * 28.0f);
	uint32_t dry = 0u;
	if (CeMultiBeIsAboveWater(groundZ, waterZ, 1.15f)) dry |= CE_DRY_HERE;
	if (DryProbe(myPos + fwd * 10.0f, waterZ, 0.9f))   dry |= CE_DRY_FWD;
	if (DryProbe(myPos - fwd * 10.0f, waterZ, 0.9f))   dry |= CE_DRY_BACK;
	if (DryProbe(myPos - right * 10.0f, waterZ, 0.9f)) dry |= CE_DRY_LEFT;
	if (DryProbe(myPos + right * 10.0f, waterZ, 0.9f)) dry |= CE_DRY_RIGHT;
	if ((dry & CE_DRY_LEFT) && DryProbe(myPos - right * 8.0f, waterZ, 1.15f) && DryProbe(myPos - right * 16.0f, waterZ, 1.15f))
		dry |= CE_DRY_DASH_L;
	if ((dry & CE_DRY_RIGHT) && DryProbe(myPos + right * 8.0f, waterZ, 1.15f) && DryProbe(myPos + right * 16.0f, waterZ, 1.15f))
		dry |= CE_DRY_DASH_R;
	if (CeMultiBeIsAboveWater(obs.aheadGroundZ, waterZ, 1.15f)) dry |= CE_DRY_AHEAD_28;
	obs.dryMask = dry;

	// Strictly read-only weapon inspection (never calls GetCurrentWeapon()!).
	int wepIdx = -1, wepCount = 0;
	CWeapon *curWep = ReadCurrentWeaponReadOnly(be, isJet, &wepIdx, &wepCount);
	obs.currentWeaponSlot = wepIdx;
	obs.weaponCount = wepCount;
	if (curWep)
	{
		obs.weaponReady = (curWep->IsActive() != FALSE && curWep->ReadyToFire() != FALSE) ? 1u : 0u;
		obs.weaponCanCharge = (curWep->CanCharge() != FALSE) ? 1u : 0u;
		obs.weaponFullyCharged = (curWep->FullyCharged() != FALSE) ? 1u : 0u;
		obs.weaponChargeRatio = CeAiClamp(curWep->GetCharge(), 0.0f, 1.0f);
		obs.weaponIsLock = (curWep->GetMaxLocks() > 0) ? 1u : 0u;
		obs.weaponActiveLocks = obs.weaponIsLock ? be->CountLocks() : 0;
		obs.weaponLocksFinished = (obs.weaponIsLock && be->LocksFinished() != FALSE) ? 1u : 0u;
		obs.weaponLaunchSpeed = curWep->GetLaunchVelocity();
		CBattleEngineData *cfg = be->GetConfiguration();
		const int store = curWep->GetAmmoStore();
		if (cfg && store >= 0 && store < kBattleEngineStores)
		{
			const auto valPtr = CePlayerHooksAccess::CeBeStoreSlot<CePlayerHooksAccess::BeStoreValueTag>::ptr;
			const auto ovhPtr = CePlayerHooksAccess::CeBeStoreSlot<CePlayerHooksAccess::BeStoreOverheatTag>::ptr;
			const auto heatPtr = CePlayerHooksAccess::CeBeStoreSlot<CePlayerHooksAccess::BeStoreHeatTag>::ptr;
			const bool isEnergy = heatPtr ? ((be->*heatPtr)[store] != FALSE) : (cfg->mStoreHeat[store] != FALSE);
			const bool isOverheated = ovhPtr ? ((be->*ovhPtr)[store] != FALSE) : (isEnergy && !obs.weaponReady);
			const float curVal = valPtr ? (be->*valPtr)[store] : 0.0f;
			const float maxVal = cfg->mStoreValue[store];
			obs.weaponIsEnergy = isEnergy ? 1u : 0u;
			obs.weaponOverheated = isOverheated ? 1u : 0u;
			obs.weaponHeatOrAmmo = (valPtr && maxVal > 1e-6f)
				? CeAiClamp(curVal / maxVal, 0.0f, 1.0f)
				: CeAiClamp(1.0f - curWep->GetReadiness(), 0.0f, 1.0f);
			obs.weaponAmmoCount = isEnergy ? 0 : static_cast<int32_t>(valPtr ? curVal : maxVal);
		}
	}

	// Collect fog-of-war-filtered entities into static BSS buffer using stack ListIterator & read-only handle/spawnSeq lookup.
	uint32_t entCount = 0u;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u; u = it.Next())
	{
		if (!u || u == be || u->IsDying() || u->IsShuttingDown() || !u->Active())
			continue;
		const FVector uPos = u->GetPos();
		if (!CeMultiBeIsFinitePos(uPos.X, uPos.Y, uPos.Z))
			continue;
		const float dx = uPos.X - myPos.X;
		const float dy = uPos.Y - myPos.Y;
		const float dz = uPos.Z - myPos.Z;
		const float dSq = dx * dx + dy * dy + dz * dz;
		const bool isFriendly = (u->GetAllegiance() == be->GetAllegiance());
		if (!CePlayerObsEntityVisible(isFriendly, u->OnScanner() != FALSE, dSq))
			continue;

		const uint32_t distQ = CePlayerObsQuantizeDistSq(dSq);
		const uint32_t spawnSeq = CeEntitySpawnSeqLookup(u);
		const CeEntityHandle handle = CeEntityHandleLookup(u);

		// Keep the closest CE_PLAYER_OBS_MAX_ENTITIES strictly by (distSqQuantized, spawnSeq).
		uint32_t insertAt = entCount;
		if (entCount >= static_cast<uint32_t>(CE_PLAYER_OBS_MAX_ENTITIES))
		{
			uint32_t worst = 0u;
			for (uint32_t k = 1u; k < entCount; ++k)
			{
				const bool worse = (obs.entities[k].distSqQuantized > obs.entities[worst].distSqQuantized) ||
				                   (obs.entities[k].distSqQuantized == obs.entities[worst].distSqQuantized &&
				                    obs.entities[k].spawnSeq > obs.entities[worst].spawnSeq);
				if (worse) worst = k;
			}
			const bool better = (distQ < obs.entities[worst].distSqQuantized) ||
			                    (distQ == obs.entities[worst].distSqQuantized && spawnSeq < obs.entities[worst].spawnSeq);
			if (!better) continue;
			insertAt = worst;
		}
		else
		{
			++entCount;
		}

		CePlayerEntityObs &e = obs.entities[insertAt];
		e.handle = handle;
		e.spawnSeq = spawnSeq;
		e.distSqQuantized = distQ;
		e.allegiance = static_cast<int32_t>(u->GetAllegiance());
		e.targetClass = ClassifyTarget(u);
		e.isFriendly = isFriendly ? 1u : 0u;
		e.airborne = (u->IsA(THING_TYPE_AIR_UNIT) != FALSE) ? 1u : 0u;
		e.inWater = (u->IsInWater() != FALSE) ? 1u : 0u;
		e.isBattleEngine = (u->IsA(THING_TYPE_BATTLE_ENGINE) != FALSE) ? 1u : 0u;
		e.isBaseBuilding = (u->IsA(THING_TYPE_BUILDING) != FALSE) ? 1u : 0u;
		e.isRepairPad = 0u;
		if ((isFriendly || u->GetAllegiance() == kIndependentAllegiance) && u->GetUnitData() && u->GetUnitData()->mName)
		{
			const char *nm = u->GetUnitData()->mName;
			if (ContainsAsciiICase(nm, "Repair Pad") || ContainsAsciiICase(nm, "Health Pad"))
				e.isRepairPad = 1u;
		}
		e.reserved = 0u;
		e.pos[0] = uPos.X; e.pos[1] = uPos.Y; e.pos[2] = uPos.Z;
		const FVector uVel = u->GetVelocity();
		e.vel[0] = uVel.X; e.vel[1] = uVel.Y; e.vel[2] = uVel.Z;
		const float uMax = u->GetMaxLife();
		e.lifeRatio = (uMax > 0.01f) ? CeAiClamp(u->GetLife() / uMax, 0.0f, 1.0f) : 1.0f;
	}
	obs.entityCount = entCount;
	CePlayerObsSortEntities(obs.entities, obs.entityCount);

	// Populate read-only weapon range bounds against the nearest visible hostile (or self pos).
	if (curWep)
	{
		FVector rangeRef = myPos;
		for (uint32_t i = 0u; i < obs.entityCount; ++i)
		{
			if (!obs.entities[i].isFriendly)
			{
				rangeRef = FVector(obs.entities[i].pos[0], obs.entities[i].pos[1], obs.entities[i].pos[2]);
				break;
			}
		}
		const float minR = curWep->GetMinRange(rangeRef);
		float maxR = curWep->GetActualMaxRange(rangeRef);
		if (!CeAiIsFinite(maxR) || maxR <= 5.0f)
			maxR = curWep->GetMaxRange(rangeRef);
		obs.weaponMinRange = (CeAiIsFinite(minR) && minR > 0.0f) ? minR : 0.0f;
		obs.weaponMaxRange = (CeAiIsFinite(maxR) && maxR > 0.0f) ? maxR : 0.0f;
	}

	// Toggle-gated Commander squads (zeroed when CE_TOGGLE_COMMANDER is OFF).
	if (toggles & CE_TOGGLE_COMMANDER)
	{
		CeCmdSquad cmdSquads[CE_PLAYER_OBS_MAX_SQUADS];
		const int nSq = CeCmdListForTeam(obs.allegiance, cmdSquads, CE_PLAYER_OBS_MAX_SQUADS);
		uint32_t outSq = 0u;
		for (int i = 0; i < nSq && outSq < static_cast<uint32_t>(CE_PLAYER_OBS_MAX_SQUADS); ++i)
		{
			if (!cmdSquads[i].visible) continue;
			CePlayerSquadObs &sq = obs.squads[outSq++];
			sq.squadHandle = CeCmdSquadHandle(cmdSquads[i].ordinal);
			sq.ordinal = cmdSquads[i].ordinal;
			sq.friendly = cmdSquads[i].friendly ? 1u : 0u;
			sq.commandable = cmdSquads[i].commandable ? 1u : 0u;
			sq.deploys = cmdSquads[i].deploys ? 1u : 0u;
			sq.crossesWater = cmdSquads[i].crossesWater ? 1u : 0u;
			sq.members = cmdSquads[i].members;
			sq.verb = cmdSquads[i].verb;
			sq.x = cmdSquads[i].x;
			sq.y = cmdSquads[i].y;
			sq.lifeRatio = (cmdSquads[i].maxLife > 0.01f) ? CeAiClamp(cmdSquads[i].life / cmdSquads[i].maxLife, 0.0f, 1.0f) : 1.0f;
		}
		obs.squadCount = outSq;
	}

	// Toggle-gated headless Play-Any-Unit candidates (zeroed when CE_TOGGLE_PLAY_UNIT is OFF).
	if (toggles & CE_TOGGLE_PLAY_UNIT)
	{
		CePlayUnitSlotCandidate cands[CE_PLAYER_OBS_MAX_TAKEABLE];
		const int nTake = CePlayUnitSlotsListTakeable(obs.allegiance, cands, CE_PLAYER_OBS_MAX_TAKEABLE);
		obs.takeableCount = static_cast<uint32_t>(nTake);
		for (int i = 0; i < nTake; ++i)
		{
			CePlayerTakeableObs &t = obs.takeable[i];
			t.unitHandle = cands[i].unitHandle;
			t.spawnSeq = cands[i].spawnSeq;
			t.classId = cands[i].classId;
			t.x = cands[i].x;
			t.y = cands[i].y;
			t.z = cands[i].z;
			t.lifeRatio = cands[i].lifeRatio;
		}
	}
}

} // namespace

const CePlayerObs *CePlayerLastObs(uint8_t slot)
{
	if (slot >= CE_PLAYER_MAX_SLOTS || !gSlotObsValid[slot])
		return nullptr;
	return &gSlotObs[slot];
}

void CePlayerHooksRegisterObsSink(CePlayerObsSinkFn sink)
{
	gObsSink = sink;
}

extern "C" void PortCePlayerHooksLevelReset(void)
{
	for (int i = 0; i < CE_PLAYER_MAX_SLOTS; ++i)
	{
		gSlotObsValid[i] = false;
		CeAiTacticalL2Reset(&gTacticalState[i]);
	}
	CePlayUnitSlotsReset();
	PortCeAiStrategistLevelReset();
	gLastActTick = 0u;
}

extern "C" void PortCePlayerHooksAct(uint32_t sim_tick)
{
	if (!PortCeLayer() || !CeMultiBeAiHostActive())
		return;
	if (sim_tick <= gLastActTick)
		PortCePlayerHooksLevelReset();
	gLastActTick = sim_tick;

	const uint32_t toggles = CePolicyActiveToggles();
	const uint32_t matchSeed = CeMultiBeAiHostMatchSeed();
	const int slotCount = CeMultiBeAiHostSlotCount();

	if (toggles & CE_TOGGLE_PLAY_UNIT)
		CePlayUnitSlotsTick(sim_tick);

	// Strictly ascending slot order 0..slotCount-1 for cross-peer determinism.
	for (int slot = 0; slot < slotCount && slot < CE_PLAYER_MAX_SLOTS; ++slot)
	{
		CBattleEngine *be = CeMultiBeAiHostSlotEngine(slot);
		if (!be || be->IsDying())
		{
			gSlotObsValid[slot] = false;
			continue;
		}
		const bool isAi = CeMultiBeAiHostSlotIsAi(slot);
		if (!isAi && !gObsSink)
			continue;

		BuildSlotObs(static_cast<uint8_t>(slot), be, sim_tick, matchSeed, toggles, gSlotObs[slot]);
		gSlotObsValid[slot] = true;

		// Publish snapshot to optional Layer-3 lock-free double buffer inside player_act (R1).
		if (gObsSink)
			gObsSink(static_cast<uint8_t>(slot), &gSlotObs[slot]);

		if (!isAi)
			continue;

		// Optional Layer-3 prior (NULL when absent or in online lockstep v1).
		// Throttled to Layer-3 cadence (every 60 SIM ticks = 0.33 Hz at 20 Hz SIM, plus tick 1).
		const CeStrategicPrior *prior = nullptr;
		if ((toggles & CE_TOGGLE_AI_STRATEGIST) && !CeMultiBeAiHostIsOnlineMatch())
		{
			if (sim_tick == 1u || (sim_tick % 60u) == 0u)
				PortCeAiStrategistPublishObs(static_cast<uint8_t>(slot), &gSlotObs[slot]);
			prior = PortCeAiStrategistPoll(static_cast<uint8_t>(slot));
		}

		// Layer 1: Deterministic Battle Engine Combat Pilot (always active on AI slots).
		CeAiPilotL1TickSlot(slot, gSlotObs[slot], prior);

		// Layer 2: Toggle-gated Tactical Commander & Headless Possession (0 CPU when toggles OFF).
		if (toggles & (CE_TOGGLE_COMMANDER | CE_TOGGLE_PLAY_UNIT))
		{
			// Reconcile cached possession handle against authoritative lease table before L2 decides.
			gTacticalState[slot].possessedUnit = (toggles & CE_TOGGLE_PLAY_UNIT)
				? CePlayUnitSlotUnit(static_cast<uint8_t>(slot))
				: kCeEntityHandleInvalid;
			CeCommanderIntent cmd = {};
			CePossessIntent possess = {};
			CeAiTacticalL2Decide(&gTacticalState[slot], gSlotObs[slot], prior, &cmd, &possess);
			if (cmd.valid && (toggles & CE_TOGGLE_COMMANDER))
			{
				int why = CE_WHY_NONE;
				CeCmdIssueForSlot(static_cast<uint8_t>(slot), gSlotObs[slot].allegiance,
				                  cmd.squadHandle, cmd.verb, cmd.goalX, cmd.goalY, &why, nullptr);
			}
			if (possess.valid && (toggles & CE_TOGGLE_PLAY_UNIT))
			{
				if (possess.release)
				{
					CePlayUnitSlotRelease(static_cast<uint8_t>(slot));
					gTacticalState[slot].possessedUnit = kCeEntityHandleInvalid;
				}
				else
				{
					bool hasLease = (CePlayUnitSlotUnit(static_cast<uint8_t>(slot)) == possess.unitHandle);
					if (!hasLease)
						hasLease = CePlayUnitSlotTake(static_cast<uint8_t>(slot), gSlotObs[slot].allegiance,
						                              possess.unitHandle, nullptr);
					if (!hasLease || !CePlayUnitSlotDrive(static_cast<uint8_t>(slot), possess.goalX, possess.goalY,
					                                      possess.fire != 0u, possess.aimX, possess.aimY, possess.aimZ, sim_tick))
					{
						gTacticalState[slot].possessedUnit = kCeEntityHandleInvalid;
					}
				}
			}
		}
	}
}

#else

const CePlayerObs *CePlayerLastObs(uint8_t) { return nullptr; }
void CePlayerHooksRegisterObsSink(CePlayerObsSinkFn) {}
extern "C" void PortCePlayerHooksLevelReset(void) {}
extern "C" void PortCePlayerHooksAct(uint32_t) {}

#endif
