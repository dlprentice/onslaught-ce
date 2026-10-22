// BoatGuide.cpp: reconstructed from BoatGuide.obj (00415c80..00415fb1); Stuart's release has no copy. A boat's
// guide turns towards its destination (away from it in mode 2) and drives forward once it is facing roughly that
// way; mode 3 turns without moving.

#include "common.h"
#include "unit.h"
#include "DebugLog.h"
#include "BoatGuide.h"

// FUNCTION: 0x00415c80 init:ZERO_FVECTOR
// FUNCTION: 0x00415ca0 init:ID_FMATRIX

//******************************************************************************************
CBoatGuide::CBoatGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x00415d70
{
}

//******************************************************************************************
// d is built in place and the first distance is tested inline, as in ThunderheadGuide.obj. In VC6's global optimizer
// (C2 0x107130cb) a d copied from the subtraction's temporary would hand every d read that temporary's fields, so
// Normalise would reuse the first distance's Y*Y. Built in place, a d.Y read folded before the expression table keeps
// its load and one folded by loop 1 becomes the field itself: the inline test's and the second distance's reads fold in
// loop 1 and share one Y*Y, while Normalise's magnitude, folded earlier, squares its own load of d.Y, which the dead
// `Y *= m` also reads (00415e94..00415eac); only X*X is shared. The full distance retail stores and never reads
// (00415e90..00415eae) is what d.Normalise() leaves of a dead vector. The walk is published as ori * forward, as the
// mech and infantry guides' is: the product's first multiply comes before the add of 0x14c.
void CBoatGuide::Unknown3()	// FUNCTION: 0x00415d90
{
	if (mUnit->IsDying())
		return;

	mUnit->mUnknown14C = FVector(0, 0, 0);
	float	current = mUnit->mCurrentOrientation.mYaw;
	FVector	d = mDestination - mUnit->GetPos();
	if (!mMode)
		return;
	if (d.MagnitudeXYSq() < 0.01f)
		return;

	float	yaw;
	switch (mMode)
	{
	case 1:
		yaw = d.Azimuth();
		break;
	case 2:
		yaw = d.Azimuth() - PI;
		break;
	case 3:
		yaw = d.Azimuth();
		break;
	default:
		LOG.AddMessage("ERROR : Unknown Terrain guide state!");
		break;
	}
	mUnit->mUnknown120.mYaw = yaw;

	float	distanceSq = d.MagnitudeXYSq();
	d.Normalise();
	if (distanceSq < 0.01f)
	{
		mMode = 0;
		return;
	}
	if (((float)fabs(AngleDifference(yaw, current)) > 0.2f) && (distanceSq < 0.1f))
		return;
	if (mMode == 3)
		return;

	float	speed = mUnit->Unknown111() / 5.0f;
	FVector	forward(0, speed, 0);
	FMatrix	&ori = mUnit->GetOrientation();
	mUnit->mUnknown14C = ori * forward;
}
