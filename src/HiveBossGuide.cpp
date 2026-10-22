// HiveBossGuide.cpp: reconstructed from HiveBossGuide.obj (004806a0..0048090f); Stuart's release has no copy. The hive
// boss's guide moves it along each axis at its script-set top speed towards its destination, and never lower than
// its minimum height.

#include "common.h"
#include "unit.h"
#include "Guide.h"
#include "map.h"

#include <math.h>

// FUNCTION: 0x004806a0 init:ZERO_FVECTOR
// FUNCTION: 0x004806c0 init:ID_FMATRIX

//******************************************************************************************
void CHiveBossGuide::Unknown3()	// FUNCTION: 0x00480790
{
	FVector	pos = mUnit->GetPos();
	FVector	v(0, 0, 0);
	float	climb = MAP.Collide(pos);
	climb -= ((CHiveBoss *)mUnit)->mMinHeight;
	climb -= pos.Z;

	if (fabs(mDestination.Z - pos.Z) < mMaxVelocity[2])
		v.Z = 0;
	else if (mDestination.Z > pos.Z)
		v.Z = (climb < mMaxVelocity[2]) ? climb : mMaxVelocity[2];
	else
		v.Z = -mMaxVelocity[2];

	if (fabs(mDestination.X - pos.X) < mMaxVelocity[0])
		v.X = 0;
	else if (mDestination.X > pos.X)
		v.X = mMaxVelocity[0];
	else
		v.X = -mMaxVelocity[0];

	if (fabs(mDestination.Y - pos.Y) < mMaxVelocity[1])
		v.Y = 0;
	else if (mDestination.Y > pos.Y)
		v.Y = mMaxVelocity[1];
	else
		v.Y = -mMaxVelocity[1];

	// below its minimum height it climbs, as fast as it may
	if (climb < 0)
	{
		float	fastest = -mMaxVelocity[2];
		if (climb < fastest)
			climb = fastest;
		v.Z = climb;
	}

	mUnit->SetVelocity(v);
}

//******************************************************************************************
void CHiveBossGuide::GoToPoint(FVector point, BOOL inOverride)	// FUNCTION: 0x00480910
{
	mDestination = point;
}
