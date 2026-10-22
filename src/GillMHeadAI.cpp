// GillMHeadAI.cpp: reconstructed from GillMHeadAI.obj (0047a9e0..0047b47c); Stuart's release has no copy. The
// GillM's head looks round within PI/4 either side of the body, sweeping 0.3 each way, fires 100 ahead along its
// aim while its weapon is ready, and drops a target that falls outside its arc.

#include "common.h"
#include "unit.h"
#include "component.h"
#include "squad.h"
#include "world.h"
#include "map.h"
#include "eventmanager.h"
#include "game.h"
#include "GillMHeadAI.h"

// FUNCTION: 0x0047a9e0 init:ZERO_FVECTOR
// FUNCTION: 0x0047aa00 init:ID_FMATRIX

// File-local copies of maths.h's FMatrix * FMatrix and FMatrix * FVector for the fire point below (a labelled stand-in;
// names ours). MulMM brackets the first product of row 0's middle component; MulMV leaves row 0's Y product
// unbracketed. With maths.h's bodies (the copies written plainly give the same code) Unknown9 differs; see its comment.
static inline FMatrix MulMM(const FMatrix &a, const FMatrix &m)
{
	return FMatrix(
				   FVector(a.Row[0].X * m.Row[0].X + a.Row[0].Y * m.Row[1].X + a.Row[0].Z * m.Row[2].X,
						   (a.Row[0].X * m.Row[0].Y) + a.Row[0].Y * m.Row[1].Y + a.Row[0].Z * m.Row[2].Y,
						   a.Row[0].X * m.Row[0].Z + a.Row[0].Y * m.Row[1].Z + a.Row[0].Z * m.Row[2].Z),
				   FVector(a.Row[1].X * m.Row[0].X + a.Row[1].Y * m.Row[1].X + a.Row[1].Z * m.Row[2].X,
						   a.Row[1].X * m.Row[0].Y + a.Row[1].Y * m.Row[1].Y + a.Row[1].Z * m.Row[2].Y,
						   a.Row[1].X * m.Row[0].Z + a.Row[1].Y * m.Row[1].Z + a.Row[1].Z * m.Row[2].Z),
				   FVector(a.Row[2].X * m.Row[0].X + a.Row[2].Y * m.Row[1].X + a.Row[2].Z * m.Row[2].X,
						   a.Row[2].X * m.Row[0].Y + a.Row[2].Y * m.Row[1].Y + a.Row[2].Z * m.Row[2].Y,
						   a.Row[2].X * m.Row[0].Z + a.Row[2].Y * m.Row[1].Z + a.Row[2].Z * m.Row[2].Z));
}
static inline FVector MulMV(const FMatrix &a, const FVector &v) { return FVector((a.Row[0].X * v.X) + a.Row[0].Y * v.Y + (a.Row[0].Z * v.Z), (a.Row[1].X * v.X) + (a.Row[1].Y * v.Y) + (a.Row[1].Z * v.Z), (a.Row[2].X * v.X) + (a.Row[2].Y * v.Y) + (a.Row[2].Z * v.Z)); }

//******************************************************************************************
// The aim yaw starts at 0 before Unknown3 and keeps it without a target: retail stores the 0 in a frame slot before
// the call (0047aaec) and loads it at the join (0047ab4d). Assigned in an else from a variable known to hold 0, the
// constant is propagated and loaded from a 0.0 constant instead. sweep shares the slot later (0047ac16, 0047ac8e).
// The pitch matrix is constructed from its angles: the constructor expands, and inside it the rows' vector
// constructor iterator and MakeRotationF are called out of line (0047ad00, 0047ad12), as nested sites of a small
// budget. Declared first and rotated with MakeRotationF, both expand at top level.
// STAND-IN (a labelled explicit concession, 2026-10-03; listed in the README
// under "Matches and steps that rely on a labelled stand-in"). What: the fire point multiplies through MulMM and MulMV
// above, file-local copies of maths.h's matrix products: MulMM brackets `(a.Row[0].X * m.Row[0].Y)`, the first product
// of row 0's middle component, and MulMV leaves row 0's `a.Row[0].Y * v.Y` unbracketed. Why: retail sums row 0's middle
// component X, Z, Y; the bracket's extra IL node gives that order, and the node it adds would push `lea ecx,[esp+0x78]`
// from the end of one 81-node scheduling region into the next, which dropping one live bracket of the vector product
// undoes. With maths.h's own bodies: shape 6, 8 instructions, 19 bytes differ. How: brackets
// emit no code; they add or remove IL nodes. The same two edits in maths.h match this function but lose 16 (the M*M
// bracket alone) to 68 other matches, so the products are copied here. Concession: the copies and the bracket
// placement are ours; Stuart's maths.h is lost, so the original spelling is unknown.
void CGillMHeadAI::Unknown9(CEvent *event)	// FUNCTION: 0x0047aad0
{
	if (mForUnit->IsDying())
		return;

	float	yaw = 0;
	Unknown3();
	if (mTarget.ToRead())
	{
		FVector	aim;
		mTarget->GetTargetablePos(&aim);
		FVector	d = aim - mForUnit->GetPos();
		yaw = (d.Azimuth());	// bracketed lone value, Stuart's habit (BattleEngine.cpp:990 `if ((weapon) && ...)`); load-bearing: without it 56/72/56
	}

	float	body = ((CComponent *)mForUnit)->mParent->mCurrentOrientation.mYaw;
	if (((float)fabs(AngleDifference(yaw, body))) > PI / 4)	// bracketed lone value (Stuart's habit, as above); load-bearing: without it 56/72/56, without both 30/40/48
		mTarget = NULL;

	if (mForUnit->AnyWeaponUnknown50a290())
	{
		float	head = mForUnit->Unknown92();
		float	sweep;
		if (!mSweepRight)
		{
			sweep = -0.3f;
			if ((float)fabs(AngleDifference(head, body - 0.3f)) < 0.01f)
				mSweepRight = TRUE;
		}
		else
		{
			sweep = 0.3f;
			if ((float)fabs(AngleDifference(head, body + 0.3f)) < 0.01f)
				mSweepRight = FALSE;
		}

		FMatrix	pitch(0, 0.35f, 0);
		FVector	fire = MulMV(MulMM(mForUnit->GetOrientation(), pitch), FVector(0, 100.0f, 0)) + mForUnit->GetPos();	// stand-in: see above
		mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&fire);
		mForUnit->GuideUnknown6(mForUnit->GetPos() + FMatrix(body + sweep, 0, 0) * FVector(0, 10.0f, 0));
	}
	else
		mForUnit->GuideUnknown6(mForUnit->GetPos() + FMatrix(body, 0, 0) * FVector(0, 10.0f, 0));
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + 0.1f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
float CGillMHeadAI::Unknown3()	// FUNCTION: 0x0047afc0
{
	float	delay = 0.5f;
	Unknown4();
	if ((mTarget.ToRead()) && (MAP.GetWaterLevel() > mForUnit->GetPos().Z))
	{
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mForUnit->Unknown86();
		mTarget->GetTargetablePos(&mAimPos);
		FMatrix	&ori = mForUnit->GetOrientation();
		mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&(FVector(ori.Row[0].Y * 100.0f, ori.Row[1].Y * 100.0f, ori.Row[2].Y * 100.0f) + mForUnit->GetPos()));
		mForUnit->Unknown74();
		delay = 0;
	}
	return delay;
}

//******************************************************************************************
void CGillMHeadAI::Unknown4()	// FUNCTION: 0x0047b090
{
	if ((mTarget.ToRead()) && (mTarget->IsDying()))
		mTarget = NULL;
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
CUnit *CGillMHeadAI::Unknown11()	// FUNCTION: 0x0047b100
{
	mCanFireAtTarget = 0;
	mTargetInReach = 0;
	SPtrSet<CThing>	*things = &WORLD.mThings;
	if (mForUnit->mAllegiance == kMuspellAllegiance)
		things = &WORLD.mSet20;
	else if (mForUnit->mAllegiance == kForsetiAllegiance)
		things = &WORLD.mSet30;

	CUnit	*best = NULL;
	float	bestPriority = -999999.0f;
	ListIterator<CThing>	it(things);
	for (CThing *thing = it.First(); thing; thing = it.Next())
	{
		FVector	d = thing->GetPos() - mForUnit->GetPos();
		float	distanceSq = d.MagnitudeSq();
		CUnit	*unit;
		if (thing->GetThingType() & THING_TYPE_UNKNOWN_20000000)
			unit = ((CSquad *)thing)->Unknown74();
		else if (thing->GetThingType() & THING_TYPE_UNIT)
			unit = (CUnit *)thing;
		else
			continue;
		if ((!unit) || (!mForUnit->IsTargetAlligence(unit->GetAllegiance())) || (!mForUnit->Unknown4fb3d0(unit)))
			continue;
		CUnitData	*data = mForUnit->mUnitData;
		float	range = (1.0f - unit->GetStealth() / 100.0f) * data->mMaxTargetRange;
		if (!(distanceSq < range * range))
			continue;

		float	p = 0;
		float	priority;
		if (mForUnit->mUnitData->mUnknown128)
			priority = GAME.FloatRandom() * 8.0f;
		else
		{
			ULONG	type = unit->GetThingType();
			CUnitData	*data = mForUnit->mUnitData;
			if (type & THING_TYPE_VEHICLE)
				p = data->mAttackPriority[1];
			else if (type & THING_TYPE_INFANTRY)
				p = data->mAttackPriority[4];
			else if (type & THING_TYPE_AIR_UNIT)
				p = data->mAttackPriority[5];
			else if (type & THING_TYPE_COMPONENT)
				p = data->mAttackPriority[6];
			else if (type & THING_TYPE_EMPLACEMENT)
				p = data->mAttackPriority[0];
			else if (type & THING_TYPE_BUILDING)
				p = data->mAttackPriority[2];
			else if (type & THING_TYPE_NAVAL)
				p = data->mAttackPriority[3];
			if (!unit->IsAThreat())
				priority = 0;
			else
				priority = p;
		}

		if (priority > bestPriority)
			bestPriority = priority;
		else if (priority != bestPriority)
			continue;

		// within PI/4 of the body's heading
		float	angle = AngleDifference((unit->GetPos() - mForUnit->GetPos()).Azimuth(), ((CComponent *)mForUnit)->mParent->mCurrentOrientation.mYaw);
		if (angle < -PI)
			angle += PI_M2;
		else if (angle > PI)
			angle -= PI_M2;
		if (!((float)fabs(angle) < PI / 4))
			continue;

		mForUnit->Unknown4fb840(unit);
		float	distance = sqrtf(distanceSq);
		if ((mForUnit->GetMinRange(unit->GetPos()) <= distance) && (!(mForUnit->GetMaxRange(unit->GetPos()) < distance)))
			best = unit;
	}

	if (best)
	{
		Unknown4ffdd0(best, FALSE);
		mForUnit->Unknown4fb840(mTarget.ToRead());
		mUnknown10 = 0;
		return mTarget.ToRead();
	}
	mTarget = NULL;
	mUnknown10 = 0;
	return mTarget.ToRead();
}
