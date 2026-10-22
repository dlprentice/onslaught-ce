// UnitAI.cpp: reconstructed from UnitAI.obj (004fe620..004ffefa); Stuart's release has no copy (his eventmanager.cpp
// includes "UnitAi.h" and reads GetForUnit). A unit's AI: it follows its waypoints (event 3001), fights (3000) or
// follows the emitter of the unit that launched it (3002); event 3003 restarts whichever it was doing.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "squad.h"
#include "world.h"
#include "map.h"
#include "waypoint.h"
#include "eventmanager.h"
#include "game.h"
#include "collisionseekingthing.h"
#include "DebugText.h"
#include "DebugLog.h"

// FUNCTION: 0x004fe620 init:ZERO_FVECTOR
// FUNCTION: 0x004fe640 init:ID_FMATRIX

enum EUnitAIEvent
{
	UNIT_AI_FIGHT = 3000,
	UNIT_AI_FOLLOW_WAYPOINTS,
	UNIT_AI_FOLLOW_EMITTER,
	UNIT_AI_RESTART
};

//******************************************************************************************
CUnitAI::CUnitAI(CUnit *unit, CInitThing *init)	// FUNCTION: 0x004fe710
{
	mForUnit = unit;
	mAIMode = -1;
	mUnknown10 = 0;
	mUnknown14 = 1;
	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	mWaypoint = (CThing *)init->mTarget;
	mEmitterType = 0;
	mEmitterIndex = 1;
	mSpawnedBy = init->mSpawnedBy;

	if (mSpawnedBy.ToRead())
	{
		mEmitterType = init->mWaypointPath;
		Unknown7();
	}
	else
	{
		if (mForUnit->GetMissionScript())
			EVENT_MANAGER.AddEvent(READY_SCRIPT, mForUnit, NEXT_FRAME);
		if (mWaypoint.ToRead())
			Unknown5();
		else
			Unknown6();
	}

	if (unit->mUnitData->mSweeping)
		NewSweep();
}

//******************************************************************************************
BOOL CUnitAI::Unknown5()	// FUNCTION: 0x004fea30
{
	if (mWaypoint.ToRead())
	{
		if (mAIMode == 0)
		{
			char	temp[100];
			sprintf(temp, "%s CANT start following waypoints cos it already was !!!", mForUnit->_GetClassName());
			DBT.Out(temp);
			LOG.AddMessage(temp);
		}
		else
		{
			mAIMode = 0;
			EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_WAYPOINTS, this, EVENT_MANAGER.GetTime());
			return TRUE;
		}
	}
	return FALSE;
}

//******************************************************************************************
// A waypoint step: it takes the next waypoint within 4, fights at once when it has a target, and reschedules when it has
// none. With no waypoint left it stops, holds and fights; with none to begin with it just fights. The target case has
// its own fight call and those two share ONE, after the blocks (`if (waypoint) reschedule else Unknown6()`): that is
// retail's block order and register picks (004feb4d, 004feb6c); a flag chain, or a call in each arm, is not.
void CUnitAI::Unknown8(CEvent *event)	// FUNCTION: 0x004feac0
{
	CThing	*waypoint = mWaypoint.ToRead();
	if (waypoint)
	{
		FVector	d = waypoint->GetPos() - mForUnit->GetPos();
		if ((d.MagnitudeXY() < 4.0f) && (((CWaypoint *)waypoint)->mNext.ToRead() != waypoint))
			mWaypoint = ((CWaypoint *)waypoint)->mNext.ToRead();
		waypoint = mWaypoint.ToRead();
		if (waypoint)
		{
			mForUnit->GoToPoint(waypoint->GetPos(), FALSE);
			Unknown4();
			if (mTarget.ToRead())
			{
				Unknown6();
				return;
			}
		}
		else
		{
			mForUnit->Stop();
			mForUnit->Unknown82();
		}
	}
	if (waypoint)
		EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_WAYPOINTS, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() + 1.0f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
	else
		Unknown6();
}

//******************************************************************************************
BOOL CUnitAI::Unknown6()	// FUNCTION: 0x004febe0
{
	if (mAIMode == 1)
	{
		char	temp[100];
		sprintf(temp, "%s CANT start fighting cos it already was !!!", mForUnit->_GetClassName());
		DBT.Out(temp);
		LOG.AddMessage(temp);
		return FALSE;
	}
	mAIMode = 1;
	EVENT_MANAGER.AddEvent(UNIT_AI_FIGHT, this, EVENT_MANAGER.GetTime());
	return TRUE;
}

//******************************************************************************************
void CUnitAI::Unknown9(CEvent *event)	// FUNCTION: 0x004fec60
{
	if (mForUnit->IsDying())
		return;
	if ((mForUnit->mDeployState == 1) || (mForUnit->mDeployState == 2))
		return;
	if (!mAIMode)
		return;

	CSquad	*squad = mForUnit->mSquad.ToRead();
	if (!squad)
	{
		if (mTarget.ToRead())
		{
			if (!mForUnit->Unknown4fb670(mTarget.ToRead()))
				mForUnit->GuideUnknown6(mTarget->GetPos());
		}
		else
			mForUnit->Stop();
	}
	else if (squad->Unknown83() == 1)
	{
		if (mTarget.ToRead())
		{
			switch (mForUnit->Unknown4fb670(mTarget.ToRead()))
			{
			case 0:
				mForUnit->GuideUnknown6(mTarget->GetPos());
				break;
			case 1:
				mForUnit->GuideUnknown7(mTarget->GetPos());
				break;
			case 2:
				mForUnit->GuideUnknown5(mTarget->GetPos());
				break;
			}
		}
		else
			mForUnit->Stop();
	}

	BOOL	swept = FALSE;
	if ((mForUnit->mUnitData->mSweeping) && (mForUnit->AnyWeaponUnknown50a290()))
	{
		if (fabsf(mSweepYaw - mSweepYawEnd) > 0.001f)
			mSweepYaw += (mSweepYawEnd - mSweepYawStart) * (1.0f / 35.0f);
		if (fabsf(mSweepPitch - mSweepPitchEnd) > 0.001f)
			mSweepPitch += (mSweepPitchEnd - mSweepPitchStart) * (1.0f / 35.0f);

		FVector	d = mAimPos - mForUnit->GetPos();
		FMatrix	ori(mSweepYaw, mSweepPitch, 0);
		mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&(ori * d + mForUnit->GetPos()));
		swept = TRUE;
	}

	float	delay = Unknown3();
	if (swept)
		delay = 0;
	EVENT_MANAGER.AddEvent(UNIT_AI_FIGHT, this, EVENT_MANAGER.GetTime() + delay, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// GetForUnit() spends the inline budget that sends the FVector constructors out of line, as retail has them. The unit's data
// is read through a local (`data`): retail's term order in the matrix product above it (README, how a value is read); without
// it two rows of that product are 4 instructions off.
float CUnitAI::Unknown3()	// FUNCTION: 0x004fef40
{
	Unknown4();
	if ((GetForUnit()->Unknown84()) && (mTarget.ToRead()))
	{
		float	delay;
		if (GetForUnit()->mUnitData->mSweeping)
		{
			mTarget->GetTargetablePos(&mAimPos);
			NewSweep();
			FVector	d = mAimPos - GetForUnit()->GetPos();
			FMatrix	ori(mSweepYaw, mSweepPitch, 0);
			FVector	aim = ori * d + mForUnit->GetPos();
			GetForUnit()->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&aim);
			delay = 0;
		}
		else
		{
			GetForUnit()->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
			delay = GAME.FloatRandom() + 0.5f;
		}
		GetForUnit()->Unknown74();
		if (GetForUnit()->mUnitData->mUnknown128)
		{
			mTarget = NULL;
			GetForUnit()->Unknown4fb840(NULL);
			mUnknown10 = 0;
		}
		return delay;
	}

	if ((mCanFireAtTarget) && (mTarget.ToRead()))
	{
		GetForUnit()->Unknown4fb840(mTarget.ToRead());
		if ((!GetForUnit()->mUnitData->mSweeping) && (GetForUnit()->mUnknown168 != 1))
		{
			mTarget->GetTargetablePos(&mAimPos);
			GetForUnit()->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
		}
		GetForUnit()->Unknown86();
		return GetForUnit()->GetWeaponUnknownA0_88() + 0.1f;
	}

	CUnit	*unit = GetForUnit();
	unit->mUnknown1EC = 0;
	unit->mUnknown1E8 = 0;
	CUnitData	*data = unit->mUnitData;
	if (data->mUnknown108)
		unit->StartUndeploying();
	if (GetForUnit()->mUnknown110)
		return GAME.FloatRandom() + 1.5f;
	return GAME.FloatRandom() * 2.0f + 3.0f;
}

//******************************************************************************************
void CUnitAI::HandleEvent(CEvent *event)	// FUNCTION: 0x004ff330
{
	BOOL	ok = TRUE;
	if ((mForUnit->mDeployState == 1) || (mForUnit->mDeployState == 2))
		ok = FALSE;

	if (mForUnit->GetAIState() == AI_ONF)
		Unknown3();
	else if (ok)
	{
		switch (event->GetEventNum())
		{
		case UNIT_AI_FOLLOW_WAYPOINTS:
			if (mForUnit->mActive)
			{
				Unknown8(event);
				return;
			}
			break;
		case UNIT_AI_FIGHT:
			if ((mForUnit->GetAIState() == AI_ON) && (mForUnit->mActive))
			{
				Unknown9(event);
				return;
			}
			break;
		case UNIT_AI_FOLLOW_EMITTER:
			Unknown10(event);
			return;
		case UNIT_AI_RESTART:
			if (mAIMode == 1)
				EVENT_MANAGER.AddEvent(UNIT_AI_FIGHT, this, NEXT_FRAME, START_OF_FRAME, NULL, (CScheduledEvent *)event);
			else if (mAIMode == 0)
				EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_WAYPOINTS, this, NEXT_FRAME, START_OF_FRAME, NULL, (CScheduledEvent *)event);
			else if (mAIMode == 2)
				EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_EMITTER, this, NEXT_FRAME, START_OF_FRAME, NULL, (CScheduledEvent *)event);
			return;
		}
	}
	EVENT_MANAGER.AddEvent(UNIT_AI_RESTART, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 2.0f + 2.0f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// Near miss: register roles around the squad's slot 72 call (004ff5fc..004ff637). The in-reach flag is read through a local
// (`reach`) in the squad branch: retail's order of the squared terms below it (README, how a value is read); without it
// the function is 18 shape / 32 instructions off.
void CUnitAI::Unknown4()	// FUNCTION: 0x004ff4f0
{
	if ((mTarget.ToRead()) && (mTarget->IsDying()))
		mTarget = NULL;

	CSquad	*squad = mForUnit->mSquad.ToRead();
	if ((squad) && (!squad->Unknown83()))
	{
		mCanFireAtTarget = 0;
		mTargetInReach = 0;
		if (!mTarget.ToRead())
			return;
		if (mForUnit->IsLiveUnit(mTarget.ToRead()))
		{
			mForUnit->Unknown4fb840(mTarget.ToRead());
			BOOL	reach = mForUnit->Unknown4fb5a0(mTarget.ToRead());
			mTargetInReach = reach;
			if (reach)
				mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		}
		else
			mTarget = NULL;
		return;
	}

	if (mForUnit->Unknown98())
	{
		ListIterator<CSquad>	squads(&WORLD.mSquadNB);
		for (CSquad *s = squads.First(); s; s = squads.Next())
		{
			if (s == mForUnit->mSquad.ToRead())
				continue;
			CUnit	*leader = s->Unknown73();
			if ((!leader) || (!mForUnit->IsTargetAlligence(leader->GetAllegiance())) || (!mForUnit->Unknown4fb3d0(leader)))
				continue;
			FVector	d = s->Unknown72() - mForUnit->GetPos();
			CUnitData	*data = mForUnit->mUnitData;
			float	range = (1.0f - s->Unknown84() / 100.0f) * data->mMaxTargetRange;
			float	dsq = d.MagnitudeSq();
			if (dsq < range * range)
				mForUnit->Unknown4fdad0(s->Unknown73());
		}
	}

	if ((mTarget.ToRead()) && (mUnknown10))
	{
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mTargetInReach = mForUnit->Unknown4fb5a0(mTarget.ToRead());
		if (mTargetInReach)
			mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		else
			mCanFireAtTarget = 0;
		return;
	}
	Unknown11();
}

//******************************************************************************************
CUnit *CUnitAI::Unknown11()	// FUNCTION: 0x004ff710
{
	if ((mTarget.ToRead()) && (mTarget->GetStealth() == 0) && (mUnknown14) && (mForUnit->IsLiveUnit(mTarget.ToRead())))
	{
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mTargetInReach = mForUnit->Unknown4fb5a0(mTarget.ToRead());
		if (mTargetInReach)
			mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		else
			mCanFireAtTarget = 0;
		return mTarget.ToRead();
	}

	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	SPtrSet<CThing>	*things = &WORLD.mThings;
	if (mForUnit->mAllegiance == kMuspellAllegiance)
		things = &WORLD.mSet20;
	else if (mForUnit->mAllegiance == kForsetiAllegiance)
		things = &WORLD.mSet30;

	CUnit	*best = NULL;
	float	bestScore = 0;
	float	bestPriority = -999999.0f;
	ListIterator<CThing>	it(things);
	for (CThing *thing = it.First(); thing; thing = it.Next())
	{
		CUnit	*unit;
		if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
			unit = ((CSquad *)thing)->Unknown74();
		else if (thing->GetThingType() & THING_TYPE_UNIT)
			unit = (CUnit *)thing;
		else
			continue;
		if ((!unit) || (!mForUnit->IsLiveUnit(unit)) || (!mForUnit->IsTargetAlligence(unit->GetAllegiance())) || (!mForUnit->Unknown4fb3d0(unit)))
			continue;

		FVector	d = thing->GetPos() - mForUnit->GetPos();
		float	distanceSq = d.MagnitudeSq();
		CUnitData	*data = mForUnit->mUnitData;
		float	range = (1.0f - unit->GetStealth() / 100.0f) * data->mMaxTargetRange;
		if (!(distanceSq < range * range))
			continue;

		float	priority = 0;
		if (mForUnit->mUnitData->mUnknown128)
			priority = GAME.FloatRandom() * 8.0f;
		else if ((unit->IsAThreat()) || (mForUnit->mUnitData->mIgnoreThreats))
		{
			ULONG	type = unit->GetThingType();
			if (type & THING_TYPE_VEHICLE)
				priority = mForUnit->mUnitData->mAttackPriority[1];
			else if (type & THING_TYPE_INFANTRY)
				priority = mForUnit->mUnitData->mAttackPriority[4];
			else if (type & THING_TYPE_AIR_UNIT)
				priority = mForUnit->mUnitData->mAttackPriority[5];
			else if (type & THING_TYPE_EMPLACEMENT)
				priority = mForUnit->mUnitData->mAttackPriority[0];
			else if (type & THING_TYPE_BUILDING)
				priority = mForUnit->mUnitData->mAttackPriority[2];
			else if (type & THING_TYPE_NAVAL)
				priority = mForUnit->mUnitData->mAttackPriority[3];
			if ((type & THING_TYPE_COMPONENT) && (priority < mForUnit->mUnitData->mAttackPriority[6]))
				priority = mForUnit->mUnitData->mAttackPriority[6];
		}

		if (priority > bestPriority)
		{
			bestPriority = priority;
			bestScore = 0;
		}
		else if (priority != bestPriority)
			continue;

		float	distance = sqrtf(distanceSq);
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
				mCanFireAtTarget = mForUnit->CanFireAt(mTarget.ToRead(), FALSE);
		}
	}
	else
		mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}

//******************************************************************************************
BOOL CUnitAI::Unknown7()	// FUNCTION: 0x004ffb60
{
	if ((mSpawnedBy.ToRead()) && (mEmitterType))
	{
		mAIMode = 2;
		mEmitterReleaseTime = EVENT_MANAGER.GetTime() + 10.0f;
		EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_EMITTER, this, EVENT_MANAGER.GetTime());
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
// The emitter-follow AddEvent comes after the cst code under an outcome flag, which leaves no code. C2 then sees the cst
// block before the follow call: the chooser gives the cst block's mForUnit load ecx (004ffd7d), and the shared AddEvent
// tail and the return label stay in the follow block, whose epilogue (004ffd75) the no-script exit jumps to. With the
// follow code as an early-return arm C2 put the follow block first (a near miss, 3 bytes).
void CUnitAI::Unknown10(CEvent *event)	// FUNCTION: 0x004ffbb0
{
	if ((!mSpawnedBy.ToRead()) || (mSpawnedBy->IsDying()))
	{
		if (mForUnit->GetVulnerable() == TRUE)
		{
			mForUnit->StartDieProcess();
			return;
		}
	}

	BOOL	follow = FALSE;
	if ((mSpawnedBy.ToRead()) && (mSpawnedBy->Unknown97()) && (mEmitterType) && (mEmitterReleaseTime > EVENT_MANAGER.GetTime()))
	{
		FVector	pos;
		FMatrix	ori;
		mSpawnedBy->GetEmitter(mEmitterType, mEmitterIndex, pos, ori);
		if ((pos.X != 0) || (pos.Y != 0) || (pos.Z != 0))
		{
			float	h = MAP.Collide(pos) - 0.1f;
			if (pos.Z > h)
				pos.Z = h;

			FVector	d = pos - mForUnit->GetPos();
			if (mForUnit->GetThingType() & THING_TYPE_AIR_UNIT)
			{
				if (d.MagnitudeSq() < 6.25f)
					mEmitterIndex++;
			}
			else
			{
				if (d.MagnitudeSq() < 0.5625f)
					mEmitterIndex++;
			}
			mForUnit->GoToPoint(pos, TRUE);
			follow = TRUE;
		}
	}

	if (!follow)
	{
		CCollisionSeekingThing	*cst = mForUnit->GetCST();
		if (cst)
			cst->mIgnoreThing = NULL;
		Unknown6();
		CUnit	*unit = mForUnit;
		if (unit->GetMissionScript())
			EVENT_MANAGER.AddEvent(READY_SCRIPT, unit, NEXT_FRAME);
	}
	else
		EVENT_MANAGER.AddEvent(UNIT_AI_FOLLOW_EMITTER, this, GAME.FloatRandom() * 0.1f + EVENT_MANAGER.GetTime() + 0.1f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
void CUnitAI::Unknown4ffdd0(CUnit *unit, BOOL a)	// FUNCTION: 0x004ffdd0
{
	mTarget = unit;
	mForUnit->Unknown4fb840(unit);
	mUnknown10 = a;
}

//******************************************************************************************
void CUnitAI::NewSweep()	// FUNCTION: 0x004ffe00
{
	mSweepYawStart = GAME.FloatRandom() * 0.04f + 0.04f;
	mSweepPitchStart = GAME.FloatRandom() * 0.04f + 0.04f;
	if (GAME.FloatRandom() > 0.5f)
		mSweepYawStart = -mSweepYawStart;
	if (GAME.FloatRandom() > 0.5f)
		mSweepPitchStart = -mSweepPitchStart;
	mSweepYawEnd = -mSweepYawStart;
	mSweepYaw = mSweepYawStart;
	mSweepPitchEnd = -mSweepPitchStart;
	mSweepPitch = mSweepPitchStart;
}
