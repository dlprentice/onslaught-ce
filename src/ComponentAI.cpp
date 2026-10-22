// ComponentAI.cpp: reconstructed from ComponentAI.obj (00428d90..0042917f); Stuart's release has no copy. The AI of a
// bomber's components: it drops a target that is dying, and picks the nearest of the most important targets in
// reach, firing when one is within 7.

#include "common.h"
#include "unit.h"
#include "squad.h"
#include "world.h"
#include "game.h"
#include "UnitAI.h"

// FUNCTION: 0x00428d90 init:ZERO_FVECTOR
// FUNCTION: 0x00428db0 init:ID_FMATRIX

//******************************************************************************************
void CComponentBomberAI::Unknown4()	// FUNCTION: 0x00428e80
{
	if ((mTarget.ToRead()) && (mTarget->IsDying()))
		mTarget = NULL;
	Unknown11();
}

//******************************************************************************************
CUnit *CComponentBomberAI::Unknown11()	// FUNCTION: 0x00428eb0
{
	mTargetInReach = 0;
	mCanFireAtTarget = 0;
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

		float	p = 0;
		float	priority;
		if (mForUnit->mUnitData->mUnknown128)
			priority = GAME.FloatRandom() * 8.0f;
		else
		{
			ULONG	type = unit->GetThingType();
			CUnitData	*data = mForUnit->mUnitData;
			if (type & THING_TYPE_VEHICLE)
				p = data->mAttackPriority[1];
			else if (type & THING_TYPE_INFANTRY)
				p = data->mAttackPriority[4];
			else if (type & THING_TYPE_AIR_UNIT)
				p = data->mAttackPriority[5];
			else if (type & THING_TYPE_COMPONENT)
				p = data->mAttackPriority[6];
			else if (type & THING_TYPE_EMPLACEMENT)
				p = data->mAttackPriority[0];
			else if (type & THING_TYPE_BUILDING)
				p = data->mAttackPriority[2];
			else if (type & THING_TYPE_NAVAL)
				p = data->mAttackPriority[3];
			if (!unit->IsAThreat())
				priority = 0;
			else
				priority = p;
		}

		if (priority > bestPriority)
		{
			bestPriority = priority;
			bestScore = 0;
		}
		else if (priority != bestPriority)
			continue;

		float	score = 1000.0f - distance;
		if (score > bestScore)
		{
			bestScore = score;
			best = unit;
		}
	}

	if (best)
	{
		Unknown4ffdd0(best, FALSE);
		if (mForUnit->IsLiveUnit(mTarget.ToRead()))
		{
			FVector	d = best->GetPos() - mForUnit->GetPos();
			if (d.MagnitudeXY() < 7.0f)
				mCanFireAtTarget = mTargetInReach = TRUE;
		}
	}
	else
		mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}
