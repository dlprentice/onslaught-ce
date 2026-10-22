// BoatAI.cpp: reconstructed from BoatAI.obj (00415aa0..00415c73); Stuart's release has no copy. A boat's AI steps
// along its waypoints once a second while it can move (slot 76), taking the next within 4.

#include "common.h"
#include "unit.h"
#include "eventmanager.h"
#include "waypoint.h"
#include "BoatAI.h"

// FUNCTION: 0x00415aa0 init:ZERO_FVECTOR
// FUNCTION: 0x00415ac0 init:ID_FMATRIX

//******************************************************************************************
void CBoatAI::Unknown8(CEvent *event)	// FUNCTION: 0x00415b90
{
	if (mWaypoint.ToRead())
	{
		if (mForUnit->Unknown76())
		{
			mForUnit->GoToPoint(mWaypoint->GetPos(), FALSE);
			FVector	d = mWaypoint->GetPos() - mForUnit->GetPos();
			if (d.Y * d.Y + d.X * d.X + d.Z * d.Z < 16.0f)
				mWaypoint = ((CWaypoint *)mWaypoint.ToRead())->mNext.ToRead();
			Unknown3();
		}
		EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + 1.0f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
	}
	else
		Unknown6();
}
