// SentinelAI.cpp: reconstructed from SentinelAI.obj (004df0b0..004df3cc); Stuart's release has no copy. A sentinel's
// AI fires a sweep, as CUnitAI's sweeping units do (004fef40), once its turret faces the target within 0.1.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"

// FUNCTION: 0x004df0b0 init:ZERO_FVECTOR
// FUNCTION: 0x004df0d0 init:ID_FMATRIX

//******************************************************************************************
// Reconstructed inline getter; its original source name is unknown. This grouping leaves the turret's vector
// construction and matrix product out of line (004df1dd, 004df201), while the later sweep arithmetic expands.
static inline FMatrix GetSentinelTurretOrientation(CUnit *unit)
{
	FMatrix turret(unit->mUnknownE0, 0, 0);
	return turret * unit->GetOrientation();
}

float CSentinelAI::Unknown3()	// FUNCTION: 0x004df1a0
{
	float	delay = 0.5f;
	Unknown4();
	if (mTarget.ToRead())
	{
		float facing = CEulerAngles(GetSentinelTurretOrientation(mForUnit)).mYaw;
		mTarget->GetTargetablePos(&mAimPos);
		FVector	d = mAimPos - mForUnit->GetPos();
		if (mForUnit->Unknown84() && ((float)fabs(AngleDifference(facing, d.Azimuth())) < 0.1f))
		{
			NewSweep();
			d = mAimPos - mForUnit->GetPos();
			FMatrix ori(mSweepYaw, mSweepPitch, 0);
			// The aim temporary lasts through the call. A named aim retains another 16-byte slot in this VC6 build;
			// passing it directly reproduces the complete retail section, including the 0x88-byte frame.
			mForUnit->WeaponUnknown509140((SINT)mTarget.ToRead(), (SINT)&(ori * d + mForUnit->GetPos()));
			mForUnit->Unknown74();
			delay = 0;
		}
		else
		{
			mForUnit->Unknown4fb840(mTarget.ToRead());
			mForUnit->Unknown86();
		}
	}
	return delay;
}
