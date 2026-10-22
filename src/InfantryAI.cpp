// InfantryAI.cpp: reconstructed from InfantryAI.obj (00489f40..0048a2cc); Stuart's release has no copy. An
// infantryman's AI fights as CUnitAI's slot 9 does (004fec60), but always from its own position: it closes on its
// target while out of reach, and starts a new sweep whenever its weapon has stopped.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "eventmanager.h"

// FUNCTION: 0x00489f40 init:ZERO_FVECTOR
// FUNCTION: 0x00489f60 init:ID_FMATRIX

//******************************************************************************************
// Near miss (12 bytes), as CUnitAI's slot 9: retail takes the aim point's address after the matrix product's last
// multiply (0048a218); this build takes it before.
void CInfantryAI::Unknown9(CEvent *event)	// FUNCTION: 0x0048a030
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
			mForUnit->GuideUnknown6(mTarget->GetPos());
			break;
		case 1:
			mForUnit->GoToPoint(mTarget->GetPos(), FALSE);
			break;
		case 2:
			mForUnit->GuideUnknown5(mTarget->GetPos());
			break;
		}
	}
	else
		mForUnit->Stop();

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
