// GroundAttackAI.cpp: reconstructed from GroundAttackAI.obj (0047b480..0047baf5); Stuart's release has no copy. A
// ground-attack aircraft's AI: it flies its waypoints, then attacks, opening its bay and closing on the target until
// it is within a random 15..17.5, when it closes the bay and pulls away 50 ahead until it is a random 20..40 out.
// A wingman keeps within 100 of the player.

#include "common.h"
#define GROUNDATTACK_FULL
#include "unit.h"
#include "component.h"
#include "player.h"
#include "BattleEngine.h"
#include "eventmanager.h"
#include "game.h"
#include "waypoint.h"
#include "GroundAttackAI.h"

// FUNCTION: 0x0047b480 init:ZERO_FVECTOR
// FUNCTION: 0x0047b4a0 init:ID_FMATRIX

// Open the bay and go in.
static inline void Attack(CGroundAttackAI *ai)
{
	ai->mAttacking = TRUE;
	ai->mRange = GAME.FloatRandom() * 2.5f + 15.0f;
	((CGroundAttackAircraft *)ai->mForUnit)->OpenBay();
}

//******************************************************************************************
// A waypoint step, as CDiveBomberAI's. The reloaded waypoint is tested twice: first to go to it or stop (Stop,
// Unknown82), then for Unknown4 and the target test; a fight, a stopped unit and one with no waypoint make the one
// Unknown6 call. At register allocation C2 keeps that order: the stop arm sits between GoToPoint and Unknown4, so its
// vtable loads (Stop's, deleted afterwards because eax already holds it from 0047b5c9, and Unknown82's in edx) take
// their scratch picks there, and Unknown6's load follows Unknown4's: edx, then eax and ecx for the reschedule's event
// and lea (0047b64e, 0047b659), as in retail. The final layout pass then moves the Unknown4 part up behind GoToPoint
// and copies the small Unknown6 block into the stop arm (0047b628). Matched. The stop arm
// in a flag chain after the fight call loaded its own Unknown6 vtable right after Unknown82's, into eax.
void CGroundAttackAI::Unknown8(CEvent *event)	// FUNCTION: 0x0047b570
{
	CThing	*waypoint = mWaypoint.ToRead();
	BOOL	fight = TRUE;
	if (waypoint)
	{
		FVector	d = waypoint->GetPos() - mForUnit->GetPos();
		if ((d.MagnitudeXY() < 4.0f) && (((CWaypoint *)waypoint)->mNext.ToRead() != waypoint))
			mWaypoint = ((CWaypoint *)waypoint)->mNext.ToRead();
		waypoint = mWaypoint.ToRead();
		if (waypoint)
			mForUnit->GoToPoint(waypoint->GetPos(), FALSE);
		else
		{
			mForUnit->Stop();
			mForUnit->Unknown82();
		}
		if (waypoint)
		{
			Unknown4();
			fight = ((mTarget.ToRead()) && (mForUnit->Unknown4fb5a0(mTarget.ToRead())));
		}
	}
	if (fight)
		Unknown6();
	else
		EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.1f + 0.1f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
	Attack(this);
}

//******************************************************************************************
// The bracketed components of the GoToPoint offsets are values of their own (README 'How a value is read'), which places
// the argument's setup as retail. The gap's offset is taken from a copy of the aim, which orders the squared terms of its
// magnitude as retail does (0047b82e; found by a readform pass); the guide call keeps the aim itself.
void CGroundAttackAI::Unknown9(CEvent *event)	// FUNCTION: 0x0047b6d0
{
	if ((!mAIMode) || (mForUnit->IsDying()))
		return;
	if (mTarget.ToRead())
		Unknown3();

	if (mForUnit->GetUnitDataUnknown120())
	{
		// a wingman keeps with the player
		CPlayer	*player = GAME.GetPlayer(0);
		CBattleEngine	*be;
		if ((player) && (be = player->GetBattleEngine()))
		{
			float	distance = (be->GetPos() - mForUnit->GetPos()).Magnitude();
			if (distance > 100.0f)
				mForUnit->GoToPoint(be->GetPos(), FALSE);
			else
			{
				Unknown4();
				if ((distance < 15.0f) && (!mTarget.ToRead()))
					mForUnit->Stop();
			}
		}
	}
	else
		Unknown4();

	if (mTarget.ToRead())
	{
		FVector	aim;
		mTarget->GetTargetablePos(&aim);
		FVector	target = aim;
		FVector	d = target - mForUnit->GetPos();
		float	radius = mTarget->GetRadius();
		if ((mTarget->GetThingType() & THING_TYPE_COMPONENT) && (((CComponent *)mTarget.ToRead())->mParent.ToRead()))
			radius = ((CComponent *)mTarget.ToRead())->mParent->GetRadius();
		float	gap = d.Magnitude() - mForUnit->GetRadius() - radius;
		if (mAttacking)
		{
			mForUnit->GuideUnknown7(aim);
			if (gap < mRange)
			{
				PullAway();
				FMatrix	&ori = mForUnit->GetOrientation();
				mForUnit->GoToPoint(mForUnit->GetPos() + FVector((ori.Row[0].Y * 50.0f), (ori.Row[1].Y * 50.0f), -(ori.Row[2].Y * 50.0f)), FALSE);
			}
		}
		else
		{
			FMatrix	&ori = mForUnit->GetOrientation();
			mForUnit->GoToPoint(mForUnit->GetPos() + FVector((ori.Row[0].Y * 50.0f), (ori.Row[1].Y * 50.0f), -(ori.Row[2].Y * 50.0f)), FALSE);
			if (gap > mRange)
				Attack(this);
		}
	}
	else
	{
		mAttacking = FALSE;
		if (Unknown5())
			return;
		if (!mForUnit->GetUnitDataUnknown120())
			mForUnit->Stop();
	}
	EVENT_MANAGER.AddEvent(3000, this, GAME.FloatRandom() * 0.5f + EVENT_MANAGER.GetTime() + 0.5f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
void CGroundAttackAI::PullAway()	// FUNCTION: 0x0047bab0
{
	mAttacking = FALSE;
	mRange = GAME.FloatRandom() * 20.0f + 20.0f;
	((CGroundAttackAircraft *)mForUnit)->CloseBay();
}
