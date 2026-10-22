// SubmarineGuide.cpp: reconstructed from SubmarineGuide.obj (004ef480..004ef9b3); Stuart's release has no copy. A
// submarine's guide: it dives and surfaces by the submarine's state (+0x250) and steers towards its destination.

#include "common.h"
#include "unit.h"
#include "Guide.h"
#include "map.h"

#include <math.h>

// FUNCTION: 0x004ef480 init:ZERO_FVECTOR
// FUNCTION: 0x004ef4a0 init:ID_FMATRIX

//******************************************************************************************
CSubmarineGuide::CSubmarineGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x004ef570
{
}

//******************************************************************************************
// It pitches and rises or sinks by its dive state, turns towards its destination (away in mode 2) and drives
// forward once facing it within 0.1. The forward-vector and yaw-matrix locals end before movement status is published.
void CSubmarineGuide::Unknown3()	// FUNCTION: 0x004ef590
{
	BOOL		moving = FALSE;
	if (!mUnit->IsDying())
	{
		float	yaw = mUnit->mCurrentOrientation.mYaw;
		float	water = MAP.GetWaterLevel();
		FVector	velocity(0, 0, 0);
		float	pitch = 0;
		switch (((CSubmarine *)mUnit)->mDiveState)
		{
		case 1:
			pitch = PI / 64;
			if (water + 10.0f < mUnit->GetPos().Z)
				velocity = FVector(0, 0, 0.004f);
			else
				velocity = FVector(0, 0, 0.001f);
			if (water + 25.0f < mUnit->GetPos().Z)
				((CSubmarine *)mUnit)->mDiveState = 0;
			break;
		case 3:
			pitch = -PI / 64;
			if (water + 10.0f < mUnit->GetPos().Z)
				velocity = FVector(0, 0, -0.004f);
			else
				velocity = FVector(0, 0, -0.001f);
			if (mUnit->GetPos().Z < water)
				((CSubmarine *)mUnit)->Unknown4ef0f0();
			break;
		case 2:
			velocity = FVector(0, 0, (water - mUnit->GetPos().Z) * 0.00625f);
			break;
		case 0:
			velocity = FVector(0, 0, (water + 25.0f - mUnit->GetPos().Z) * 0.00625f);
			break;
		}

		if (mMode)
		{
			FVector	d = mDestination - mUnit->GetPos();
			float	current = yaw;
			if (d.MagnitudeXY() > 2.0f)
			{
				yaw = d.Azimuth();
				if (mMode == 2)
				{
					yaw -= PI_M2;
					if (yaw > PI)
						yaw -= PI_M2;
					if (yaw < -PI)
						yaw += PI_M2;
				}
				if ((float)fabs(AngleDifference(current, yaw)) < 0.1f)
				{
					{
						float	speed = (mUnit->GetMaxVelocity() / 20.0f) * 2.0f;
						FVector	forward(0, speed, 0);
						FMatrix	ori(yaw, 0, 0);
						velocity += ori * forward;
					}
					moving = TRUE;
					mUnit->mUnknown174 = 1.0f;
				}
			}
		}
		mUnit->mUnknown120 = CEulerAngles(yaw, pitch, 0);
		mUnit->mUnknown14C = velocity;
		if (moving)
			return;
	}
	mUnit->mUnknown174 = 0;
}
