// WarspiteDomeAI.cpp: reconstructed from WarspiteDomeAI.obj (00504d60..0050520f); Stuart's release has no copy. The
// Warspite dome's AI picks targets as CUnitAI does (004ff710), but keeps any live target it already has and ranks
// the others by priority alone, without the random or ignore-threat choices.

#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "unit.h"
#include "squad.h"
#include "world.h"
#include "WarspiteDomeAI.h"
// FUNCTION: 0x00504d60 init:ZERO_FVECTOR
// FUNCTION: 0x00504d80 init:ID_FMATRIX

//******************************************************************************************
CUnit *CWarspiteDomeAI::Unknown11()	// FUNCTION: 0x00504e50
{
	if ((mTarget.ToRead()) && (mForUnit->IsLiveUnit(mTarget.ToRead())))
	{
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mTargetInReach = mForUnit->Unknown4fb5a0(mTarget.ToRead());
		if (mTargetInReach)
			mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		else
			mCanFireAtTarget = 0;
		return mTarget.ToRead();
	}

	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	SPtrSet<CThing>	*things = &WORLD.mThings;
	if (mForUnit->mAllegiance == kMuspellAllegiance)
		things = &WORLD.mSet20;
	else if (mForUnit->mAllegiance == kForsetiAllegiance)
		things = &WORLD.mSet30;

	CUnit	*best = NULL;
	float	bestScore = 0;
	float	bestPriority = -999999.0f;
	ListIterator<CThing>	it(things);
	for (CThing *thing = it.First(); thing; thing = it.Next())
	{
		FVector	d = thing->GetPos() - mForUnit->GetPos();
		float	distance = d.Magnitude();
		CUnit	*unit;
		if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
			unit = ((CSquad *)thing)->Unknown74();
		else if (thing->GetThingType() & THING_TYPE_UNIT)
			unit = (CUnit *)thing;
		else
			continue;
		if ((!unit) || (!mForUnit->IsTargetAlligence(unit->GetAllegiance())) || (!mForUnit->Unknown4fb3d0(unit)))
			continue;
		CUnitData	*data = mForUnit->mUnitData;
		if (!((1.0f - unit->GetStealth() / 100.0f) * data->mMaxTargetRange > distance))
			continue;

		float	priority = 0;
		if (unit->IsAThreat())
		{
			ULONG	type = unit->GetThingType();
			if (type & THING_TYPE_VEHICLE)
				priority = mForUnit->mUnitData->mAttackPriority[1];
			else if (type & THING_TYPE_INFANTRY)
				priority = mForUnit->mUnitData->mAttackPriority[4];
			else if (type & THING_TYPE_AIR_UNIT)
				priority = mForUnit->mUnitData->mAttackPriority[5];
			else if (type & THING_TYPE_EMPLACEMENT)
				priority = mForUnit->mUnitData->mAttackPriority[0];
			else if (type & THING_TYPE_BUILDING)
				priority = mForUnit->mUnitData->mAttackPriority[2];
			else if (type & THING_TYPE_NAVAL)
				priority = mForUnit->mUnitData->mAttackPriority[3];
			if ((type & THING_TYPE_COMPONENT) && (priority < mForUnit->mUnitData->mAttackPriority[6]))
				priority = mForUnit->mUnitData->mAttackPriority[6];
		}

		if (priority > bestPriority)
		{
			bestPriority = priority;
			bestScore = 0;
		}
		else if (priority != bestPriority)
			continue;

		float	score = 1000.0f - distance;
		mForUnit->Unknown4fb840(unit);
		if (mForUnit->GetMinRange(unit->GetPos()) <= distance)
		{
			if (mForUnit->GetMaxRange(unit->GetPos()) < distance)
				score += 10000.0f;
			else
				score += 1000000.0f;
		}
		if (score > bestScore)
		{
			bestScore = score;
			best = unit;
		}
	}

	if (best)
	{
		Unknown4ffdd0(best, FALSE);
		mForUnit->Unknown4fb840(mTarget.ToRead());
		if (mForUnit->IsLiveUnit(mTarget.ToRead()))
		{
			mTargetInReach = mForUnit->Unknown4fb5a0(mTarget.ToRead());
			if (mTargetInReach)
				mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		}
	}
	else
		mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}
