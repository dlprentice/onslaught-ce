// DiveBomberAI.cpp: reconstructed from DiveBomberAI.obj (004456e0..0044674f); Stuart's release has no copy. A dive
// bomber's AI escorts the player's battle engine when its unit data asks, follows its waypoints, and attacks in one
// of three ways by its weapon and target: a dive (mode 0), a lobbed attack for a falling round (1), or against
// aircraft (2).

#include "common.h"
#include "unit.h"
#include "component.h"
#include "DiveBomberAI.h"
#include "waypoint.h"
#include "weapon.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "eventmanager.h"

// FUNCTION: 0x004456e0 init:ZERO_FVECTOR
// FUNCTION: 0x00445700 init:ID_FMATRIX

//******************************************************************************************
// A waypoint step: it takes the next waypoint within 4, and fights once it can reach a target. The target case has its
// own fight call and returns; a stopped unit and one with no waypoint share the call at the end (CUnitAI::Unknown8's
// structure, 004feac0), which gives retail's block order and the stop arm's registers.
// STAND-IN: the target read passed to Unknown4fb5a0 is `(waypoint ? mTarget : mTarget)`, two
// identical arms. Retail keeps the reloaded waypoint in edi (00445822 `mov edi,[edi]`), the register of the
// &mWaypoint address web it is read through, so that address web (priority 21) must be coloured before the reloaded
// waypoint; plain, the waypoint scores 22, goes first and takes eax (node count: one counted node more in block 0 or 3,
// or one fewer in block 4 or 5, gives retail's order). The identical arms emit no code and leave no branch in the
// allocator's blocks, but this read is no longer merged with the test's read of mTarget before allocation: the test's
// value then ends in the target test, that block counts one node fewer (5 -> 4) and the waypoint scores 20. The code
// still reads mTarget once (eax for both the test and the push). Other conditions there do the same (event, mMode,
// mForUnit). Plain form: 42 instructions differ, registers only. Which source Stuart wrote is unknown.
void CDiveBomberAI::Unknown8(CEvent *event)	// FUNCTION: 0x004457d0
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
			// STAND-IN (spelling): the target is read through a conditional with two identical arms; plain `mTarget.ToRead()` is 27 bytes off.
			if ((mTarget.ToRead()) && (mForUnit->Unknown4fb5a0((waypoint ? mTarget : mTarget).ToRead())))
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
		EVENT_MANAGER.AddEvent(3001, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.1f + 0.1f), START_OF_FRAME, NULL, (CScheduledEvent *)event);
	else
		Unknown6();
}

//******************************************************************************************
// A fighting step: it keeps within 90 of an escorted battle engine, then attacks by its mode.
void CDiveBomberAI::Unknown9(CEvent *event)	// FUNCTION: 0x00445900
{
	if ((!mAIMode) || (mForUnit->IsDying()))
		return;

	BOOL	escorting = FALSE;
	float	delay;
	if (mForUnit->GetUnitDataUnknown120())
	{
		CPlayer			*player;
		CBattleEngine	*be;
		if ((player = GAME.GetPlayer(0)) && (be = player->GetBattleEngine()) && (mForUnit->mAllegiance == be->mAllegiance))
		{
			float	distance = (be->GetPos() - mForUnit->GetPos()).Magnitude();
			if (distance > 90.0f)
			{
				mForUnit->GoToPoint(be->GetPos(), escorting);
				escorting = TRUE;
			}
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
		mForUnit->Unknown4fb840(mTarget.ToRead());
		CWeapon	*weapon = mForUnit->mCurrentWeapon;
		if (weapon)
		{
			if ((weapon->mMode->mRound->mGravity * 0.025f != 0.0f) && (!weapon->mMode->mRound->mBeam) && (!weapon->mMode->mRound->mUnknown6C))
				mMode = 1;
			else if (mTarget->GetThingType() & THING_TYPE_AIR_UNIT)
				mMode = 2;
			else
				mMode = 0;
		}
	}
	else if (escorting)
		goto next;		// a skip as Stuart's DXEngine.cpp:687: flying to the battle engine with nothing to attack, the delay is left unset, as retail's (00445a95); the AddEvent written out and a return is 142 bytes off

	switch (mMode)
	{
	case 0:
		delay = Attack();
		break;
	case 1:
		delay = Unknown445f40();
		break;
	case 2:
		delay = Unknown446150();
		break;
	}
next:
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + delay, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// The end of a dive: the bay shuts, its timer restarts at 20 to 40 and, if the target is still there, the bomber pulls
// away 150 from it. An inline helper on the evidence of the inline-budget rule (the inline-budget simulator): with this arm as a helper
// the compiler calls operator-, Normalise and operator+ out of line inside the flee, as retail does (00445c60, 00445c69,
// 00445cac), and Attack emits retail's out-of-line vector functions (0040d120, 00406d50, 00401ee0; with the order
// below also the constructor 00401ec0), all four matching. STAND-IN (spelling, no code): the scaling is written out per
// component through a named float. Retail expands it (00445c6e), but this helper's nested budget is under 40, so
// `away *= 150.0f` (operator*=, 45) would be a call; the named float gives the helper the size Attack's budget needs
// (210, see Attack). What Stuart wrote here is unknown.
inline void CDiveBomberAI::Retreat()
{
	mUnknown68 = FALSE;
	mUnknown70 = GAME.FloatRandom() * 20.0f + 20.0f;
	((CDiveBomber *)mForUnit)->CloseBay();
	mUnknown64 = FALSE;
	if (mTarget.ToRead())
	{
		FVector	away = mForUnit->GetPos() - mTarget->GetPos();
		away.Normalise();
		float	scale = 150.0f;
		away.X = away.X * scale;
		away.Y = away.Y * scale;
		away.Z = away.Z * scale;
		mForUnit->GoToPoint(mForUnit->GetPos() + away, FALSE);
	}
}

// The target's radius, or its parent's when it is a component; mode 2 reads it the same way. A helper on the same kind
// of evidence: it lowers Attack's own size (and so its inline budget) and raises its spending by the helper's size,
// which is the shift the simulation needs for the flee and the dive start to be decided as retail's are; with the block
// written out the budget stays 1272 and nearly every site of both arms expands. STAND-IN (spelling, no code): the
// explicit comparisons and the nested, braced tests give the helper the size Attack's budget needs (128, see Attack);
// mode 2's spelling here is 115.
inline float CDiveBomberAI::TargetRadius()
{
	float	radius = mTarget->GetRadius();
	if ((mTarget->GetThingType() & THING_TYPE_COMPONENT) != 0)
	{
		if (((CComponent *)mTarget.ToRead())->mParent.ToRead() != NULL)
		{
			radius = ((CComponent *)mTarget.ToRead())->mParent->GetRadius();
		}
	}
	return radius;
}

//******************************************************************************************
// Mode 0, the dive: from beyond 55 (40 across) it climbs 30 over its target and starts the dive; diving, it heads for
// a point 30 below the target while the horizontal gap exceeds 20, opens its bay within that gap and retreats
// within a full 3D gap of 15; otherwise it pulls away 150 from the target. The flee is written before the dive start
// because, of the orders tried, only this one gives the budget simulation retail's decision at each site of both arms
// (the inline-budget simulator); C2 lays the dive start out first, as retail's is.
// Every arm leaves through one `return delay`: retail reads delay once, so its frame slot follows radius's and gap's
// ([esp+0x14]); with a return in each arm delay is read four times and takes the lowest slot. The gotos are ours: an
// if/else chain with one return reads it once too but adds 3 to the own size, which the helpers would have to make up.
// Inline budget (the inline-budget simulator): own size 550, so 1100. Retail calls the flee's operator+ (00445ef9 jumps into
// Retreat's call at 00445ca8) and the dive start's constructor and operator- (00445dfd, 00445e0b) while it expands the
// flee's operator-, Normalise and *=: 0 to 46 must be left after the flee's *=, so the three expanded helpers
// TargetRadius, Retreat and Unknown446400 must total 451 to 497. Their plain spellings total 409 (115, 180, 114); the
// labelled spellings in the first two give 452 (128, 210, 114).
float CDiveBomberAI::Attack()	// FUNCTION: 0x00445ad0
{
	float	delay = GAME.FloatRandom() * 0.5f + 0.5f;
	if ((mTarget.ToRead()) && (mUnknown68))
		delay = Unknown3();

	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector	aim;
		target->GetTargetablePos(&aim);
		float	radius = TargetRadius();
		float	gap = (aim - mForUnit->GetPos()).Magnitude() - mForUnit->GetRadius() - radius;
		float	gapXY = (aim - mForUnit->GetPos()).MagnitudeXY() - mForUnit->GetRadius() - radius;

		// The three `goto done` skip to the one `return delay` (a skip as Stuart's DXEngine.cpp:687), which gives `delay` the one return
		// use retail has; `return delay;` at each of them rotates the float locals' frame slots (15 bytes off), an
		// if/else-if/else structure is 958, a done flag 430.
		if (mUnknown64)
		{
			if (gap < 15.0f)
				Retreat();
			else if (gapXY > 20.0f)
				mForUnit->GoToPoint(FVector(aim.X, aim.Y, aim.Z - 30.0f), FALSE);
			else
				Unknown446400();
			goto done;
		}

		if (!((gap > 55.0f) && (gapXY > 40.0f)))
		{
			FVector	away = mForUnit->GetPos() - aim;
			away.Normalise();
			away *= 150.0f;
			mForUnit->GoToPoint(mForUnit->GetPos() + away, FALSE);
			goto done;
		}
		mUnknown64 = TRUE;
		mForUnit->GoToPoint(mTarget->GetPos() - FVector(0, 0, 30.0f), FALSE);
		goto done;
	}

	mUnknown68 = FALSE;
	if (Unknown5())
		return 0;
	if (!mForUnit->GetUnitDataUnknown120())
		mForUnit->Stop();
done:
	return delay;
}

//******************************************************************************************
// Mode 1, for a falling round: it runs in past 35 and turns back once more than a quarter turn off within that,
// aiming 30 below its target. The heading is a double, as CBomberAI's slot 9 (retail loads the current yaw before it
// computes the heading and keeps the heading in memory for AngleDifference, 00446007..00446075).
float CDiveBomberAI::Unknown445f40()	// FUNCTION: 0x00445f40
{
	float	delay = GAME.FloatRandom() * 0.5f + 0.5f;
	if (mTarget.ToRead())
		delay = Unknown3();

	CUnit	*target = mTarget.ToRead();
	if (target)
	{
		FVector			d = target->GetPos() - mForUnit->GetPos();
		CEulerAngles	current = mForUnit->GetCurrentEulerOrientation();
		float			distance = d.MagnitudeXY();
		if (distance > 35.0f)
			mUnknown6C = TRUE;
		else if (distance > 5.0f)
		{
			double	yaw = d.Azimuth();
			if ((float)fabs(AngleDifference(yaw, current.mYaw)) > PI / 2)
				mUnknown6C = FALSE;
		}

		FVector	aim;
		target->GetTargetablePos(&aim);
		aim.Z -= 30.0f;
		if (mUnknown6C)
			mForUnit->GoToPoint(aim, FALSE);
		else
			mForUnit->GuideUnknown5(aim);
		return delay;
	}

	if (Unknown5())
		return 0;
	mForUnit->Stop();
	return delay;
}

//******************************************************************************************
// Mode 2, against aircraft: with its bay open it closes on its target and, once nearer than its timer, shuts the bay
// and breaks away 50 ahead (and down); with it shut it flies on ahead until the gap is past the timer again.
// The range working value starts with the target radius and is then adjusted to the surface gap. The breakaway
// displacement is scaled in local space before its orientation transform, then reflected vertically. These value
// boundaries preserve the retail stack slots, x87 arithmetic and out-of-line call to Unknown446400 (004463b6).
float CDiveBomberAI::Unknown446150()	// FUNCTION: 0x00446150
{
	float	delay = GAME.FloatRandom() * 0.5f + 0.5f;
	if (mTarget.ToRead())
		delay = Unknown3();

	if (mTarget.ToRead())
	{
		FVector	aim;
		mTarget->GetTargetablePos(&aim);
		FVector	d = aim - mForUnit->GetPos();
		float	gap = mTarget->GetRadius();
		if ((mTarget->GetThingType() & THING_TYPE_COMPONENT) && (((CComponent *)mTarget.ToRead())->mParent.ToRead()))
			gap = ((CComponent *)mTarget.ToRead())->mParent->GetRadius();
		gap = d.Magnitude() - mForUnit->GetRadius() - gap;

		if (mUnknown68)
		{
			mForUnit->GuideUnknown7(aim);
			if (gap < mUnknown70)
			{
				mUnknown68 = FALSE;
				mUnknown70 = GAME.FloatRandom() * 20.0f + 20.0f;
				((CDiveBomber *)mForUnit)->CloseBay();
				FMatrix	&ori = mForUnit->GetOrientation();
				FVector ahead = ori * (FVector(0.0f, 1.0f, 0.0f) * 50.0f);
				ahead.Z = -ahead.Z;
				mForUnit->GoToPoint(ahead + mForUnit->GetPos(), FALSE);
			}
		}
		else
		{
			FMatrix	&ori = mForUnit->GetOrientation();
			FVector ahead = ori * (FVector(0.0f, 1.0f, 0.0f) * 50.0f);
			ahead.Z = -ahead.Z;
			mForUnit->GoToPoint(ahead + mForUnit->GetPos(), FALSE);
			if (gap > mUnknown70)
				Unknown446400();
		}
		return delay;
	}

	mUnknown68 = FALSE;
	if (Unknown5())
		return 0;
	if (!mForUnit->GetUnitDataUnknown120())
		mForUnit->Stop();
	return delay;
}

//******************************************************************************************
// It opens its bay, once, for 15 to 17.5 seconds, and heads for its target.
void CDiveBomberAI::Unknown446400()	// FUNCTION: 0x00446400
{
	if (!mUnknown68)
	{
		mUnknown68 = TRUE;
		mUnknown70 = GAME.FloatRandom() * 2.5f + 15.0f;
		((CDiveBomber *)mForUnit)->OpenBay();
	}
	if (mTarget.ToRead())
		mForUnit->GuideUnknown7(mTarget->GetPos());
}

//******************************************************************************************
// It fires by its mode: a dive fires when it can, a lobbed attack only within 7 of its target, and against aircraft
// only while it can fire at its target. Each mode owns its cleanup and assigns the common return value. This
// preserves retail's x87 delay lifetime and its mode-0 jump into mode 2's firing tail (004466f8). The default
// still returns an unset delay, as the retail load at 00446676 does.
float CDiveBomberAI::Unknown3()	// FUNCTION: 0x00446480
{
	float	delay;
	switch (mMode)
	{
	case 0:
		Unknown4();
		if (mForUnit->Unknown84())
		{
			mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
			mForUnit->Unknown74();
			delay = GAME.FloatRandom() + 0.5f;
			break;
		}
		if (mTarget.ToRead())
		{
			mTarget->GetTargetablePos(&mAimPos);
			mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
			mForUnit->Unknown86();
			delay = mForUnit->GetWeaponUnknownA0_88() + 0.1f;
			break;
		}
		{
			CUnit	*unit = mForUnit;
			unit->mUnknown1EC = 0;
			unit->mUnknown1E8 = 0;
			if (unit->mUnitData->mUnknown108)
				unit->StartUndeploying();
			delay = GAME.FloatRandom() + 0.5f;
			break;
		}

	case 1:
		delay = GAME.FloatRandom() + 0.5f;
		if ((mTarget.ToRead()) && ((mTarget->GetPos() - mForUnit->GetPos()).MagnitudeXY() < 7.0f))
		{
			mForUnit->Unknown86();
			if (mForUnit->Unknown84())
			{
				mForUnit->Unknown74();
				delay = GAME.FloatRandom() * 2.0f + 2.0f;
				break;
			}
		}
		else
		{
			CUnit	*unit = mForUnit;
			unit->mUnknown1EC = 0;
			unit->mUnknown1E8 = 0;
			if (unit->mUnitData->mUnknown108)
				unit->StartUndeploying();
		}
		break;

	case 2:
		Unknown4();
		if (mForUnit->Unknown84())
		{
			mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
			mForUnit->Unknown74();
			delay = GAME.FloatRandom() + 0.5f;
			break;
		}
		if ((mCanFireAtTarget) && (mTarget.ToRead()))
		{
			mTarget->GetTargetablePos(&mAimPos);
			mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&mAimPos);
			mForUnit->Unknown86();
			delay = mForUnit->GetWeaponUnknownA0_88() + 0.1f;
			break;
		}
		{
			CUnit	*unit = mForUnit;
			unit->mUnknown1EC = 0;
			unit->mUnknown1E8 = 0;
			if (unit->mUnitData->mUnknown108)
				unit->StartUndeploying();
			delay = GAME.FloatRandom() + 0.5f;
			break;
		}

	default:
		break;
	}
	return delay;
}
