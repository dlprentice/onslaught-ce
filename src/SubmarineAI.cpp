// SubmarineAI.cpp: reconstructed from SubmarineAI.obj (004ef250..004ef474); Stuart's release has no copy. A
// submarine's AI closes on its target while it is too far, backs off while too near, and holds its range between.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "eventmanager.h"
#include "waypoint.h"
#include "game.h"

// FUNCTION: 0x004ef250 init:ZERO_FVECTOR
// FUNCTION: 0x004ef270 init:ID_FMATRIX

//******************************************************************************************
void CSubmarineAI::Unknown9(CEvent *event)	// FUNCTION: 0x004ef340
{
	if ((!mAIMode) || (mForUnit->IsDying()))
		return;
	Unknown3();

	if (mTarget.ToRead())
	{
		SINT	band = mForUnit->Unknown4fb670(mTarget.ToRead());
		if (band == 1)
			mForUnit->GoToPoint(mTarget->GetPos(), FALSE);
		else if (band == 2)
			mForUnit->GuideUnknown5(mTarget->GetPos());
		else
			mForUnit->GuideUnknown6(mTarget->GetPos());
	}
	else
	{
		if (Unknown5())
			return;
		mForUnit->Stop();
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + GAME.FloatRandom(), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// The waypoint walk (as CBomberAI's, BomberAI.cpp). It compiles to the bomber's body, so the linker folds it into that
// one (/OPT:ICF) and the annotation names the survivor.

// FUNCTION: 0x004164b0
void CSubmarineAI::Unknown8(CEvent *event)
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
	}
	else
		Unknown6();
}
