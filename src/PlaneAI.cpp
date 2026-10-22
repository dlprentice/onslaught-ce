// PlaneAI.cpp: reconstructed from PlaneAI.obj (004d20d0..004d2489); Stuart's release has no copy. A plane's AI
// escorts the player's battle engine when its unit data asks (flying to it beyond 120, idling within 30), and
// otherwise opens its wings and attacks: it closes on its target, and once too near it breaks away, flying out past
// 70 before it turns back.

#include "common.h"
#include "unit.h"
#include "PlaneAI.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "eventmanager.h"
#include "waypoint.h"

// FUNCTION: 0x004d20d0 init:ZERO_FVECTOR
// FUNCTION: 0x004d20f0 init:ID_FMATRIX

//******************************************************************************************
void CPlaneAI::Unknown9(CEvent *event)	// FUNCTION: 0x004d21c0
{
	if (mForUnit->IsDying())
		return;
	Unknown3();

	CPlayer			*player;
	CBattleEngine	*be;
	if ((mForUnit->GetUnitDataUnknown120()) && (player = GAME.GetPlayer(0)) && (be = player->GetBattleEngine()))
	{
		float	distance = (be->GetPos() - mForUnit->GetPos()).Magnitude();
		if (distance > 120.0f)
		{
			mForUnit->GoToPoint(be->GetPos(), FALSE);
			mTarget = NULL;
		}
		else if ((distance < 30.0f) && (!mTarget.ToRead()))
			mForUnit->Stop();
	}

	if (mTarget.ToRead())
	{
		((CPlane *)mForUnit)->OpenWings();
		FVector	aim;
		mTarget->GetTargetablePos(&aim);
		CUnit	*target = mTarget.ToRead();
		FVector	d = aim - mForUnit->GetPos();
		float	distance = d.Magnitude() - mForUnit->GetRadius() - target->GetRadius();
		SINT	band;
		if (mUnknown60)
		{
			if (distance < 25.0f)
			{
				band = 2;
				mUnknown60 = 0;
			}
			else
				band = mForUnit->Unknown4fb670(mTarget.ToRead());
		}
		else
		{
			if (distance > 70.0f)
				mUnknown60 = 1;
			band = 2;
		}

		switch (band)
		{
		case 0:
			mForUnit->GoToPoint(aim, FALSE);
			break;
		case 1:
			mForUnit->GoToPoint(aim, FALSE);
			break;
		case 2:
			mForUnit->GoToPoint(mForUnit->GetPos() - (aim - mForUnit->GetPos()), FALSE);
			break;
		}
	}
	else
	{
		((CPlane *)mForUnit)->CloseWings();
		mUnknown60 = 0;
		if (Unknown5())
			return;
		if (!mForUnit->GetUnitDataUnknown120())
			mForUnit->Stop();
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.25f + 0.25f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// The waypoint walk (as CBomberAI's, BomberAI.cpp). It compiles to the bomber's body, so the linker folds it into that
// one (/OPT:ICF) and the annotation names the survivor.

// FUNCTION: 0x004164b0
void CPlaneAI::Unknown8(CEvent *event)
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
