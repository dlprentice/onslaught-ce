// FenrirMainGunAI.cpp: reconstructed from FenrirMainGunAI.obj (0044e550..0044e9bf); Stuart's release has no copy.
// The Fenrir's main gun: it takes the most important target in reach that lies within its arc ahead and within its
// weapon's range band.

#include "common.h"
#include "unit.h"
#include "squad.h"
#include "world.h"
#include "game.h"
#include "UnitAI.h"

// FUNCTION: 0x0044e550 init:ZERO_FVECTOR
// FUNCTION: 0x0044e570 init:ID_FMATRIX

//******************************************************************************************
// The component AIs' slot 4 (ComponentAI.cpp's copy is the one kept, 00428e80).
void CFenrirMainGunAI::Unknown4()
{
	if ((mTarget.ToRead()) && (mTarget->IsDying()))
		mTarget = NULL;
	Unknown11();
}

//******************************************************************************************
CUnit *CFenrirMainGunAI::Unknown11()	// FUNCTION: 0x0044e640
{
	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	SPtrSet<CThing>	*things = &WORLD.mThings;
	if (mForUnit->mAllegiance == kMuspellAllegiance)
		things = &WORLD.mSet20;
	else if (mForUnit->mAllegiance == kForsetiAllegiance)
		things = &WORLD.mSet30;

	CUnit	*best = NULL;
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
			bestPriority = priority;
		else if (priority != bestPriority)
			continue;

		// within its arc ahead
		float	angle = AngleDifference((unit->GetPos() - mForUnit->GetPos()).Azimuth(), mForUnit->GetOrientation().GetColumnY().Azimuth());
		if (angle < -PI)
			angle += PI_M2;
		else if (angle > PI)
			angle -= PI_M2;
		if (!((float)fabs(angle) < PI / 4))
			continue;

		mForUnit->Unknown4fb840(unit);
		if ((mForUnit->GetMinRange(unit->GetPos()) <= distance) && (!(mForUnit->GetMaxRange(unit->GetPos()) < distance)))
			best = unit;
	}

	if (best)
	{
		Unknown4ffdd0(best, FALSE);
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mUnknown10 = 0;
		mCanFireAtTarget = TRUE;
		mTargetInReach = TRUE;
		return mTarget.ToRead();
	}
	mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}
