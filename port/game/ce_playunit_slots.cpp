// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_playunit_slots.cpp: Headless multi-slot unit possession adapter (IUnitPossessionPort).
// Strictly simulation-only (zero camera allocation, zero controller-stack or HUD mutation).
#include "ce_playunit_slots.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "weapon.h"
#include "squad.h"
#include "map.h"
#include "ce_policy.h"
#include "ce_playunit.h"

#include <math.h>
#include <string.h>

extern bool PortCeLayer();

namespace
{

CePlayUnitSlotsCore gSlots;

bool IsNormalSquad(CSquad *squad)
{
	return squad && !strcmp(squad->_GetClassName(), "CNormalSquad");
}

void FillFacts(CUnit *u, int allegiance, CePlayUnitFacts &f)
{
	memset(&f, 0, sizeof(f));
	if (!u) return;
	const unsigned kind = static_cast<unsigned>(u->GetThingType());
	const CUnitData *d = u->GetUnitData();
	CSquad *sq = u->GetSquad();
	f.friendly = (int)u->GetAllegiance() == allegiance;
	f.alive = !u->IsDying() && !u->IsShuttingDown() && u->GetLife() > 0.f;
	f.scripted = (u->GetMissionScript() != NULL) || (sq && sq->GetMissionScript() != NULL);
	f.objective = (u->IsObjective() != FALSE);
	f.normalSquad = IsNormalSquad(sq);
	f.spawned = sq && sq->mUnknown84 != 0;
	f.armed = (u->mCurrentWeapon != NULL);
	if (!f.armed)
	{
		ListIterator<CWeapon> weapons(&u->mWeapons);
		for (CWeapon *w = weapons.First(); w && !f.armed; w = weapons.Next())
			if (w->mMode) f.armed = true;
	}
	f.aircraft = (kind & THING_TYPE_AIR_UNIT) != 0;
	f.naval = (kind & THING_TYPE_NAVAL) != 0;
	f.ordinaryFoot = (kind & THING_TYPE_INFANTRY) != 0;
	f.turret = (kind & THING_TYPE_EMPLACEMENT) != 0;
	f.vehicle = (kind & THING_TYPE_GROUND_UNIT) != 0 && !f.ordinaryFoot && !f.aircraft && !f.naval && !f.turret;
	if (d)
	{
		f.groundUnit = (d->mBehaviour == UB_GROUND_UNIT || d->mBehaviour == UB_MECH);
		f.deploys = (d->mUnknown108 != 0) || (d->mName && strstr(d->mName, "Artillery") != NULL);
	}
}

void ReturnUnitToWorld(const CeUnitPossessionSlot &lease)
{
	CUnit *u = CeEntityHandleResolveUnit(lease.unitHandle);
	if (!u || u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.f)
		return;
	if ((int)u->GetAllegiance() != lease.allegiance)
		return;
	if (u->GetAIState() != AI_OFF)
		return;

	u->GoToPoint(u->GetPos(), FALSE);
	CSquad *prevSquad = CeEntityHandleResolveSquad(lease.previousSquadHandle);
	if (prevSquad && IsNormalSquad(prevSquad) && !prevSquad->IsDying() && !prevSquad->IsShuttingDown() &&
	    (int)prevSquad->mUnknown7C == lease.allegiance && u->GetSquad() == NULL)
	{
		CNormalSquad *ns = static_cast<CNormalSquad *>(prevSquad);
		ns->Unknown67(u, 0, 0);
		u->SetAIState(ns->GetAIState());
	}
	else
	{
		u->SetAIState(static_cast<EAIState>(lease.previousAiState));
	}
}

} // namespace

int CePlayUnitSlotsListTakeable(int allegiance, CePlayUnitSlotCandidate *out, int max)
{
	if (!out || max <= 0 || !PortCeLayer()) return 0;
	if (!(CePolicyActiveToggles() & CE_TOGGLE_PLAY_UNIT)) return 0;

	int count = 0;
	ListIterator<CUnit> it(&WORLD.GetUnitNB());
	for (CUnit *u = it.First(); u && count < max; u = it.Next())
	{
		if (u->IsA(THING_TYPE_BATTLE_ENGINE)) continue;
		const CeEntityHandle h = CeEntityHandleLookup(u);
		if (h == kCeEntityHandleInvalid) continue;
		if (gSlots.IsUnitLeased(h)) continue;
		CePlayUnitFacts facts;
		FillFacts(u, allegiance, facts);
		const int classId = CePlayUnitClassFor(facts);
		if (classId < 0) continue;
		const FVector pos = u->GetPos();
		const float maxL = u->GetMaxLife();
		CePlayUnitSlotCandidate &c = out[count++];
		c.unitHandle = h;
		c.spawnSeq = CeEntitySpawnSeqLookup(u);
		c.classId = classId;
		c.x = pos.X;
		c.y = pos.Y;
		c.z = pos.Z;
		c.lifeRatio = (maxL > 0.01f) ? (u->GetLife() / maxL) : 1.0f;
	}
	return count;
}

bool CePlayUnitSlotTake(uint8_t slot, int allegiance, CeEntityHandle unitHandle, const char **outReason)
{
	if (!PortCeLayer())
	{
		if (outReason) *outReason = "CE layer disabled";
		return false;
	}
	const bool toggleOn = (CePolicyActiveToggles() & CE_TOGGLE_PLAY_UNIT) != 0u;
	CUnit *u = CeEntityHandleResolveUnit(unitHandle);
	if (!u || u->IsA(THING_TYPE_BATTLE_ENGINE))
	{
		if (outReason) *outReason = "unit unavailable";
		return false;
	}
	CePlayUnitFacts facts;
	FillFacts(u, allegiance, facts);
	// Never steal a unit actively piloted by Player 0's human Play-Any-Unit driver.
	const bool humanOwns = (slot != 0u && CePlayUnitOwnsPilot());
	CSquad *sq = u->GetSquad();
	const CeEntityHandle prevSqHandle = sq ? CeEntityHandleLookup(sq) : kCeEntityHandleInvalid;
	const int prevAi = static_cast<int>(u->GetAIState());

	// Release any previous lease owned by this slot first.
	CeUnitPossessionSlot prevLease = {};
	if (gSlots.Release(slot, &prevLease))
		ReturnUnitToWorld(prevLease);

	if (!gSlots.Take(slot, allegiance, unitHandle, prevSqHandle, prevAi, facts, toggleOn, humanOwns, outReason))
		return false;

	if (sq && IsNormalSquad(sq))
		static_cast<CNormalSquad *>(sq)->Unknown4e6f70(u);
	u->SetAIState(AI_OFF);
	return true;
}

bool CePlayUnitSlotDrive(uint8_t slot, float goalX, float goalY, bool fire, float aimX, float aimY, float aimZ, uint32_t simTick)
{
	(void)fire; (void)aimX; (void)aimY; (void)aimZ;
	if (!PortCeLayer() || !(CePolicyActiveToggles() & CE_TOGGLE_PLAY_UNIT))
		return false;
	CeUnitPossessionSlot *s = gSlots.Mutable(slot);
	if (!s || !s->active) return false;
	CUnit *u = CeEntityHandleResolveUnit(s->unitHandle);
	if (!u || u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.f || (int)u->GetAllegiance() != s->allegiance)
	{
		CePlayUnitSlotRelease(slot);
		return false;
	}
	if (isfinite(goalX) && isfinite(goalY) && (simTick == 0u || simTick >= s->lastOrderTick + 6u))
	{
		FVector dst(goalX, goalY, 0.f);
		dst.Z = MAP.Collide(dst);
		if (isfinite(dst.Z))
		{
			u->GoToPoint(dst, FALSE);
			s->lastOrderTick = simTick;
		}
	}
	return true;
}

void CePlayUnitSlotRelease(uint8_t slot)
{
	CeUnitPossessionSlot prev = {};
	if (gSlots.Release(slot, &prev))
		ReturnUnitToWorld(prev);
}

CeEntityHandle CePlayUnitSlotUnit(uint8_t slot)
{
	const CeUnitPossessionSlot *s = gSlots.Get(slot);
	return (s && s->active) ? s->unitHandle : kCeEntityHandleInvalid;
}

void CePlayUnitSlotsTick(uint32_t)
{
	if (!PortCeLayer()) return;
	for (uint8_t slot = 0; slot < CE_PLAYUNIT_MAX_SLOTS; ++slot)
	{
		const CeUnitPossessionSlot *s = gSlots.Get(slot);
		if (!s || !s->active) continue;
		CUnit *u = CeEntityHandleResolveUnit(s->unitHandle);
		if (!u || u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.f || (int)u->GetAllegiance() != s->allegiance)
			CePlayUnitSlotRelease(slot);
	}
}

void CePlayUnitSlotsReset()
{
	gSlots.Reset();
}

#else

int CePlayUnitSlotsListTakeable(int, CePlayUnitSlotCandidate *, int) { return 0; }
bool CePlayUnitSlotTake(uint8_t, int, CeEntityHandle, const char **) { return false; }
bool CePlayUnitSlotDrive(uint8_t, float, float, bool, float, float, float, uint32_t) { return false; }
void CePlayUnitSlotRelease(uint8_t) {}
CeEntityHandle CePlayUnitSlotUnit(uint8_t) { return kCeEntityHandleInvalid; }
void CePlayUnitSlotsTick(uint32_t) {}
void CePlayUnitSlotsReset() {}

#endif
