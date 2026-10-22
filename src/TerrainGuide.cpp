// TerrainGuide.cpp: reconstructed from TerrainGuide.obj (004f1dd0..004f211f); Stuart's release has no copy. A
// ground unit's guide (Sentinel, GillM, the Warspite dome) turns it towards its destination (away from it in mode 2)
// and drives forward once it is facing roughly that way; mode 3 turns without moving. BoatGuide.obj is a copy.

#include "common.h"
#include "unit.h"
#include "DebugLog.h"
#include "Guide.h"

// FUNCTION: 0x004f1dd0 init:ZERO_FVECTOR
// FUNCTION: 0x004f1df0 init:ID_FMATRIX

//******************************************************************************************
CTerrainGuide::CTerrainGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x004f1ec0
{
}

//******************************************************************************************
// As in BoatGuide.obj and ThunderheadGuide.obj, d is built in place and the first distance is tested inline, so
// Normalise squares its own load of d.Y and reuses only X*X (BoatGuide.cpp's comment). The full distance retail
// stores and never reads (004f1fe9..004f200f) is what d.Normalise() leaves of a dead vector. The walk is published as
// ori * forward, as the mech and infantry guides' is.
void CTerrainGuide::Unknown3()	// FUNCTION: 0x004f1ee0
{
	if (!mUnit->IsDying())
	{
		mUnit->mUnknown14C = FVector(0, 0, 0);
		FVector	d = mDestination - mUnit->GetPos();
		float	current = mUnit->mCurrentOrientation.mYaw;
		if (!mMode)
			return;
		if ((d.MagnitudeXYSq() < 0.01f) || (mUnit->IsDeploying()))
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
	else
		mUnit->Stop();
}
