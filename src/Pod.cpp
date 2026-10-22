// Pod.cpp: reconstructed from Pod.obj (004d34e0..004d38db); Stuart's release has no copy. A pod a script launches: it
// lobs itself at a target on the arc that lands nearest and settles when it hits the ground.

#include "common.h"
#include "unit.h"
#include "map.h"
#include "WorldPhysicsManager.h"

#include <math.h>

// FUNCTION: 0x004d34e0 init:ZERO_FVECTOR
// FUNCTION: 0x004d3500 init:ID_FMATRIX

//******************************************************************************************
void CPod::Init(CInitThing *init)	// FUNCTION: 0x004d35d0
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_UNIT | THING_TYPE_TREE;
	mLanded = TRUE;
	mLaunched = FALSE;
	SUPERTYPE::Init(init);
	mTarget = init->mPos;
}

//******************************************************************************************
void CPod::Move()	// FUNCTION: 0x004d3630
{
	SUPERTYPE::Move();
	mVelocity.Z += Gravity();
}

//******************************************************************************************
void CPod::DeclareOnGround()	// FUNCTION: 0x004d3650
{
	SUPERTYPE::DeclareOnGround();
	if (!mLanded)
	{
		Unknown82();
		mLanded = TRUE;
		mVelocity = FVector(0, 0, 0);
	}
}

//******************************************************************************************
void CPod::Launch(FVector target)	// FUNCTION: 0x004d36c0
{
	if (!mLaunched)
	{
		mTarget = target;
		mTarget.Z = MAP.Collide(target);
		Aim();
		mLaunched = TRUE;
		mLanded = FALSE;
	}
}

//******************************************************************************************
// Tries pitches from straight down to level for the arc that lands nearest the target. The horizontal displacement
// is recomputed after Gravity(), and the launch speed is applied along the matrix's local forward axis.
void CPod::Aim()	// FUNCTION: 0x004d3730
{
	float	yaw = (mTarget - mPos).Azimuth();
	float	dz = mTarget.Z - mPos.Z;
	float	v = mUnitData->mUnknownB4 / 20.0f;
	float	g = Gravity();
	float	dist = (mTarget - mPos).MagnitudeXY();
	float	best = 99999.0f;
	float	pitch;
	for (float a = -PI / 2; a < 0; a += 0.02f)
	{
		float	vv = (float)sin(a) * v;
		float	disc = vv * vv + g * dz * 2;
		if (disc > 0)
		{
			float	r = (sqrtf(disc) - vv) / g * ((float)cos(a) * v);
			if (r > 0)
			{
				float	err = (float)fabs(r - dist);
				if (err < best)
				{
					best = err;
					pitch = a;
				}
			}
		}
	}
	FMatrix	m(yaw, pitch, 0);
	mVelocity = m * FVector(0.0f, v, 0.0f);
}

//******************************************************************************************
void CPod::SetThingType(ULONG t)	// FUNCTION: 0x004d3890
{
	SUPERTYPE::SetThingType(t | THING_TYPE_CAN_DESTROY_TREES);
}

//******************************************************************************************
float CPod::Gravity()	// FUNCTION: 0x004d38b0
{
	return 0.001f;
}

//******************************************************************************************
BOOL CPod::StartDieProcess()	// FUNCTION: 0x004d38c0
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	Remove();
	return TRUE;
}

//******************************************************************************************
// The pod's answers that no code above defined. Each compiles to a body another function already has, so the linker
// folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each annotation
// names (the class header notes the slots' survivors too).

// FUNCTION: 0x0040c640
void CPod::Stop()
{
}
