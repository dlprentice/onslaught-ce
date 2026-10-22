// CarrierAI.cpp: reconstructed from CarrierAI.obj (00421cc0..00421e91); Stuart's release has no copy. A carrier's
// AI flies straight to its waypoint and stops there.

#include "common.h"
#include "unit.h"
#include "eventmanager.h"
#include "CarrierAI.h"

// FUNCTION: 0x00421cc0 init:ZERO_FVECTOR
// FUNCTION: 0x00421ce0 init:ID_FMATRIX

//******************************************************************************************
void CCarrierAI::Unknown12()	// FUNCTION: 0x00421db0
{
	CThing	*waypoint = mWaypoint.ToRead();
	if (!waypoint)
		return;

	FVector	d = waypoint->GetPos() - mForUnit->GetPos();
	mForUnit->GoToPoint(waypoint->GetPos(), FALSE);
	if (d.MagnitudeSq() < 16.0f)
	{
		mWaypoint = NULL;
		mForUnit->Stop();
		mForUnit->Unknown82();
		return;
	}
	EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + 1.0f);
}
