// MechAI.cpp: reconstructed from MechAI.obj (004a01f0..004a0927); Stuart's release has no copy. A mech's AI fights as
// the infantry's does (InfantryAI.obj), but circles a target within reach, fifty units at a step, the way a coin toss
// chose when it was made.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "eventmanager.h"
#include "game.h"

// FUNCTION: 0x004a01f0 init:ZERO_FVECTOR
// FUNCTION: 0x004a0210 init:ID_FMATRIX

//******************************************************************************************
CMechAI::CMechAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init)	// FUNCTION: 0x004a02e0
{
	mUnknown14 = 0;
	if (GAME.FloatRandom() > 0.5f)
		mCircleLeft = TRUE;
	else
		mCircleLeft = FALSE;
}

// The implicit destructor, emitted here with the vtable; ~CUnitAI inlined into it leaves only CUnitAI's vtable store
// (004a03cd), so its code is the same as ??1CUnitAI's (Boat.obj, 00415080).
// FUNCTION: 0x004a0390 ??_GCMechAI@@UAEPAXI@Z
// FUNCTION: 0x004a03b0 ??1CMechAI@@UAE@XZ

//******************************************************************************************
// Near miss: retail gives the two turn matrices their own slots in a frame 0x20 bytes larger, builds the second one's
// rows out of line (004a0647) and normalises in X, Y, Z order (004a053a); this build shares one slot.
void CMechAI::Unknown9(CEvent *event)	// FUNCTION: 0x004a0450
{
	if (mForUnit->IsDying())
		return;
	if ((mForUnit->mDeployState == 1) || (mForUnit->mDeployState == 2))
		return;
	if (!mAIMode)
		return;

	if (mTarget.ToRead())
	{
		SINT	band = mForUnit->Unknown4fb670(mTarget.ToRead());
		if (!mTargetInReach)
			band = 1;
		switch (band)
		{
		case 0:
		{
			FVector	d = mTarget->GetPos() - mForUnit->GetPos();
			d.Normalise();
			FVector	v;
			if (mCircleLeft)
				v = FMatrix(PI / 2, 0, 0) * d;
			else
				v = FMatrix(-PI / 2, 0, 0) * d;
			v *= 50.0f;
			mForUnit->GoToPoint(v + mForUnit->GetPos(), FALSE);
			break;
		}
		case 1:
			mForUnit->GoToPoint(mTarget->GetPos(), FALSE);
			break;
		case 2:
			mForUnit->GuideUnknown5(mTarget->GetPos());
			break;
		}
	}

	BOOL	swept = FALSE;
	if ((mForUnit->mUnitData->mSweeping) && (mTarget.ToRead()))
	{
		if (mForUnit->AnyWeaponUnknown50a290())
		{
			if (fabsf(mSweepYaw - mSweepYawEnd) > 0.001f)
				mSweepYaw += (mSweepYawEnd - mSweepYawStart) * (1.0f / 35.0f);
			if (fabsf(mSweepPitch - mSweepPitchEnd) > 0.001f)
				mSweepPitch += (mSweepPitchEnd - mSweepPitchStart) * (1.0f / 35.0f);
		}
		else
		{
			NewSweep();
			mTarget->GetTargetablePos(&mAimPos);
		}

		FVector	d = mAimPos - mForUnit->GetPos();
		FMatrix	ori(mSweepYaw, mSweepPitch, 0);
		mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&(ori * d + mForUnit->GetPos()));
		swept = TRUE;
	}

	float	delay = Unknown3();
	if (swept)
		delay = 0;
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime() + delay, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}
