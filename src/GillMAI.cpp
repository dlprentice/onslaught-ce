// GillMAI.cpp: reconstructed from GillMAI.obj (0047a1b0..0047a63b); Stuart's release has no copy. The GillM's AI
// goes for the nearest building or ship in its weapon's reach and, once within 15 of it (25 of a ship), stops and
// swings at it.

#include "common.h"
#include "unit.h"
#include "squad.h"
#include "world.h"
#include "eventmanager.h"
#include "game.h"
#include "GillMAI.h"

// FUNCTION: 0x0047a1b0 init:ZERO_FVECTOR
// FUNCTION: 0x0047a1d0 init:ID_FMATRIX


//******************************************************************************************
void CGillMAI::Unknown9(CEvent *event)	// FUNCTION: 0x0047a2a0
{
	if (mForUnit->IsDying())
		return;
	Unknown11();

	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector	d = target->GetPos() - mForUnit->GetPos();
		float	distance = d.Magnitude();
		float	reach = 15.0f;
		if (target->GetThingType() & THING_TYPE_NAVAL)
			reach = 25.0f;
		if (distance > reach + 3.0f)
			mForUnit->GoToPoint(target->GetPos(), FALSE);
		else if (distance > reach)
		{
			mForUnit->Stop();
			mForUnit->GuideUnknown6(mTarget->GetPos());
			((CGillM *)mForUnit)->Swing();
		}
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.5f + 0.5f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
CUnit *CGillMAI::Unknown11()	// FUNCTION: 0x0047a400
{
	if ((mTarget.ToRead()) && (!mTarget->IsDying()) && (mForUnit->IsLiveUnit(mTarget.ToRead())))
		return mTarget.ToRead();

	SPtrSet<CThing>	*things = &WORLD.mThings;
	if (mForUnit->mAllegiance == kMuspellAllegiance)
		things = &WORLD.mSet20;
	else if (mForUnit->mAllegiance == kForsetiAllegiance)
		things = &WORLD.mSet30;

	CUnit	*best = NULL;
	float	bestScore = 0;
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
		if ((!unit) || (unit->IsDying()) || (!mForUnit->IsTargetAlligence(unit->GetAllegiance())))
			continue;
		if (!(unit->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_NAVAL)))
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
			{
				mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
				mUnknown10 = 0;
				return mTarget.ToRead();
			}
		}
	}
	else
		mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}
