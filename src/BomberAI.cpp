// BomberAI.cpp: reconstructed from BomberAI.obj (004163c0..0041687b); Stuart's release has no copy. A bomber's AI:
// beyond 35 of its target it starts a run straight at it; once within 35 it keeps on until the target is behind it,
// then turns to come round again.

#include "common.h"
#include "unit.h"
#include "eventmanager.h"
#include "game.h"
#include "waypoint.h"
#include "BomberAI.h"

// FUNCTION: 0x004163c0 init:ZERO_FVECTOR
// FUNCTION: 0x004163e0 init:ID_FMATRIX

//******************************************************************************************
void CBomberAI::Unknown8(CEvent *event)	// FUNCTION: 0x004164b0
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

//******************************************************************************************
// STAND-IN (order): the heading is a double (narrowed to AngleDifference's float): retail loads the current yaw before it
// computes the heading and keeps the heading in memory for AngleDifference (00416638..004166a6); a float local did the
// reverse (83 bytes off).
void CBomberAI::Unknown9(CEvent *event)	// FUNCTION: 0x00416590
{
	if ((!mAIMode) || (mForUnit->IsDying()))
		return;
	Unknown3();

	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector			d = target->GetPos() - mForUnit->GetPos();
		CEulerAngles	current = mForUnit->GetCurrentEulerOrientation();
		float			distance = d.MagnitudeXY();
		if (distance > 35.0f)
			mRun = TRUE;
		else if (distance > 5.0f)
		{
			double	yaw = d.Azimuth();	// STAND-IN (order), see above
			if ((float)fabs(AngleDifference(yaw, current.mYaw)) > PI / 2)
				mRun = FALSE;
		}

		FVector	aim = target->GetPos();
		aim.Z = mForUnit->GetPos().Z;
		if (mRun)
			mForUnit->GoToPoint(aim, FALSE);
		else
			mForUnit->GuideUnknown5(aim);
	}
	else
	{
		if (Unknown5())
			return;
		mForUnit->Stop();
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.5f + 0.5f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
float CBomberAI::Unknown3()	// FUNCTION: 0x00416790
{
	float	delay = GAME.FloatRandom() + 0.5f;
	Unknown4();
	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector	d = target->GetPos() - mForUnit->GetPos();
		if (d.MagnitudeXY() < 15.0f)
		{
			mForUnit->Unknown86();
			if (mForUnit->Unknown84())
			{
				mForUnit->Unknown74();
				return GAME.FloatRandom() * 2.0f + 2.0f;
			}
			return delay;
		}
	}

	CUnit	*unit = mForUnit;
	unit->mUnknown1EC = 0;
	unit->mUnknown1E8 = 0;
	if (unit->mUnitData->mUnknown108)
		unit->StartUndeploying();
	return delay;
}
