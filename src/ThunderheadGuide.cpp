// ThunderheadGuide.cpp: reconstructed from ThunderheadGuide.obj (004f4d10..004f51b4); Stuart's release has no copy.
// The thunderhead's guide steers as the terrain guide does (TerrainGuide.obj), but it only moves while the step takes
// it nearer home or keeps it within 70 of it.

#include "common.h"
#include "unit.h"
#include "DebugLog.h"
#include "Guide.h"

// FUNCTION: 0x004f4d10 init:ZERO_FVECTOR
// FUNCTION: 0x004f4d30 init:ID_FMATRIX

//******************************************************************************************
CThunderheadGuide::CThunderheadGuide(CUnit *unit, FVector home) : CGuide(unit)	// FUNCTION: 0x004f4e00
{
	mHome = home;
}

//******************************************************************************************
void CThunderheadGuide::Unknown3()	// FUNCTION: 0x004f4e40
{
	if (!GetUnit()->IsDying())
	{
		FVector	velocity(0, 0, 0);
		GetUnit()->mUnknown14C = velocity;
		float	current = GetUnit()->mCurrentOrientation.mYaw;
		FVector	d = mDestination - GetUnit()->GetPos();
		if (!mMode)
			return;
		if ((d.MagnitudeXYSq() < 0.01f) || (GetUnit()->IsDeploying()))
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
		GetUnit()->mUnknown120.mYaw = yaw;

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

		float	speed = GetUnit()->Unknown111();
		FMatrix	&ori = GetUnit()->GetOrientation();
		velocity = ori * (FVector(0, speed, 0) / 5.0f);
		FVector	next = velocity + GetUnit()->GetPos();
		float	nextDistance = (next - mHome).Magnitude();
		if (((GetUnit()->GetPos() - mHome).Magnitude() > nextDistance) || (nextDistance < 70.0f))
			GetUnit()->mUnknown14C = velocity;
	}
	else
		GetUnit()->Stop();
}
