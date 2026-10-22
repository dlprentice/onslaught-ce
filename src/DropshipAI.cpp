// DropshipAI.cpp: reconstructed from DropshipAI.obj (004483a0..00448838); Stuart's release has no copy. A dropship's
// AI follows its waypoints while its doors are shut, then flies to its target, lands at a clear anchor, opens up,
// waits while its spawners unload and fights, closes and climbs away.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "waypoint.h"
#include "map.h"
#include "eventmanager.h"
#include "game.h"

// FUNCTION: 0x004483a0 init:ZERO_FVECTOR
// FUNCTION: 0x004483c0 init:ID_FMATRIX

//******************************************************************************************
// A waypoint step: while its doors are shut it flies on, taking the next waypoint within slot 94's distance. The
// flight comes first and the open-door case is the else: VC6 gives scratch registers to single-block temporaries in the
// order of the source, so the else arm's vtable load must come after the waypoint's (004484c9..00448534).
void CDropshipAI::Unknown8(CEvent *event)	// FUNCTION: 0x00448490
{
	if (mWaypoint.ToRead())
	{
		if (mForUnit->Unknown76())
		{
			if (!((CDropship *)mForUnit)->mDoorState)
			{
				mForUnit->GoToPoint(mWaypoint->GetPos(), FALSE);
				float	distance = (mWaypoint->GetPos() - mForUnit->GetPos()).MagnitudeXY();
				if (distance < mForUnit->Unknown94())
					mWaypoint = ((CWaypoint *)mWaypoint.ToRead())->mNext.ToRead();
			}
			else
			{
				Unknown6();
				return;
			}
		}
		EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + 1.0f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
	}
	else
		Unknown6();
}

//******************************************************************************************
// A fighting step, by the state of its doors.
void CDropshipAI::Unknown9(CEvent *event)	// FUNCTION: 0x00448580
{
	if ((!mAIMode) || (mForUnit->IsDying()))
		return;

	CDropship	*ship = (CDropship *)mForUnit;
	FVector		pos = ship->GetPos();
	float		ground = MAP.Collide(pos);
	float		water = MAP.GetWaterLevel();
	if (ground > water)
		ground = water;

	switch (ship->mDoorState)
	{
	case 0:
		Unknown4();
		if (mTarget.ToRead())
		{
			ship->GoToPoint(mTarget->GetPos(), FALSE);
			if (ship->CanFireAt(mTarget.ToRead(), TRUE))
				ship->mDoorState = 1;
		}
		else
			ship->Unknown100();
		break;

	case 1:
		if (ship->IsAtAnchor())
			ship->Unknown118();
		else
			ship->GoToPoint(ship->GetAnchor(), FALSE);
		break;

	case 3:
		if (ship->CanStayOpen())
		{
			if (ship->IsOnGround())
				ship->Unknown448110();
		}
		else
			ship->Unknown120();
		break;

	case 4:
		if (!ship->AllSpawnersReady())
		{
			if (ship->mAnchorFixed)
			{
				mForUnit->Unknown82();
				break;
			}
			Unknown4();
			if (mTarget.ToRead())
			{
				Unknown3();
				break;
			}
		}
		if (!ship->AnySpawnerUnknown4e4420())
			ship->Unknown448120();
		break;

	case 5:
		if (pos.Z - ground < -35.0f)
			ship->mDoorState = 0;
		break;
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() + 1.0f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
float CDropshipAI::Unknown3()	// FUNCTION: 0x004487e0
{
	mForUnit->Unknown86();
	if (mForUnit->Unknown84())
		mForUnit->Unknown74();
	return GAME.FloatRandom() + 0.5f;
}
