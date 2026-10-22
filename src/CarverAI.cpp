// CarverAI.cpp: reconstructed from CarverAI.obj (004229b0..00422e9b); Stuart's release has no copy. A carver's AI:
// it flies its waypoints with its wings closed, and fights with them open, turning away to come round again when it
// gets within 15 of its target.

#include "common.h"
#include "unit.h"
#include "round.h"
#include "mapwho.h"
#include "mapwhoentry.h"
#include "eventmanager.h"
#include "game.h"
#include "waypoint.h"
#include "CarverAI.h"

// FUNCTION: 0x004229b0 init:ZERO_FVECTOR
// FUNCTION: 0x004229d0 init:ID_FMATRIX

//******************************************************************************************
void CCarverAI::Unknown8(CEvent *event)	// FUNCTION: 0x00422aa0
{
	if (mWaypoint.ToRead())
	{
		if (mForUnit->Unknown76())
		{
			mForUnit->GoToPoint(mWaypoint->GetPos(), FALSE);
			FVector	d = mWaypoint->GetPos() - mForUnit->GetPos();
			if (d.MagnitudeXY() < 4.0f)
				mWaypoint = ((CWaypoint *)mWaypoint.ToRead())->mNext.ToRead();
			Unknown3();
		}
		EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + 1.0f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
		((CCarver *)mForUnit)->CloseWings();
	}
	else
	{
		Unknown6();
		((CCarver *)mForUnit)->CloseWings();
	}
}

//******************************************************************************************
void CCarverAI::Unknown9(CEvent *event)	// FUNCTION: 0x00422b90
{
	if (mForUnit->IsDying())
		return;
	CheckRounds();
	Unknown3();

	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector	d = target->GetPos() - mForUnit->GetPos();
		float	gap = d.Magnitude() - mForUnit->GetRadius() - target->GetRadius();
		SINT	band;
		if (mAttacking)
		{
			if (gap < 15.0f)
			{
				band = 2;
				mAttacking = FALSE;
			}
			else
				band = mForUnit->Unknown4fb670(mTarget.ToRead());
		}
		else
		{
			if (gap > 40.0f)
				mAttacking = TRUE;
			band = 2;
		}

		if (mAttacking)
			((CCarver *)mForUnit)->OpenWings();
		else
			((CCarver *)mForUnit)->CloseWings();

		switch (band)
		{
		case 0:
			mForUnit->GoToPoint(mTarget->GetPos(), FALSE);
			break;
		case 1:
			mForUnit->GoToPoint(mTarget->GetPos(), FALSE);
			break;
		case 2:
			mForUnit->GoToPoint(mForUnit->GetPos() - (mTarget->GetPos() - mForUnit->GetPos()), FALSE);
			break;
		}
	}
	else
	{
		((CCarver *)mForUnit)->CloseWings();
		mAttacking = FALSE;
		if (Unknown5())
			return;
		if (!mForUnit->GetUnitDataUnknown120())
			mForUnit->Stop();
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.25f + 0.25f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
void CCarverAI::CheckRounds()	// FUNCTION: 0x00422db0
{
	for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntryWithinRadius(mForUnit->GetPos(), 20.0f); entry; entry = MAP_WHO.GetNextEntryWithinRadius())
	{
		if (!(entry->GetThing()->GetThingType() & THING_TYPE_AMMUNITION))
			continue;
		FVector	d = entry->GetThing()->GetPos() - mForUnit->GetPos();
		if (d.Magnitude() > 20.0f)
			continue;
		if ((((CRound *)entry->GetThing())->mUnknownE8.ToRead() == mForUnit) && (((CRound *)entry->GetThing())->mData->mUnknown1C > 0))
		{
			((CCarver *)mForUnit)->SetAttacked();
			return;
		}
	}
}
