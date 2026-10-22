// EscapePod.cpp: reconstructed from EscapePod.obj (0044a9c0..0044ae10); Stuart's release has no copy. An escape pod
// a script launches: a small rocket that accelerates along its nose with its engine burning. The object also holds
// CEulerAngles' constructor from a matrix.

#include "common.h"
#include "EscapePod.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "ParticleManager.h"
#include "DebugLog.h"

#include <math.h>
#include <string.h>

// FUNCTION: 0x0044a9c0 init:ZERO_FVECTOR
// FUNCTION: 0x0044a9e0 init:ID_FMATRIX

//******************************************************************************************
void CEscapePod::Init(CInitThing *init)	// FUNCTION: 0x0044aab0
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY | THING_TYPE_BUILDING;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;

	CRenderData	data;
	data.mInit[0].mThing = this;
	data.mRTID[0] = 1;
	strcpy(data.mInit[0].mName, "m_rocket.msh");

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	SUPERTYPE::Init(init);
	CParticleDescriptor	*pd = PARTICLE_SET.GetPD("Muspell Engine Small Effect");
	if (pd)
		PARTICLE_MANAGER.CreateEffect(pd, &mTrail, ZERO_FVECTOR, 0, 0);
}

//******************************************************************************************
// The thrust is the orientation times (0, 0.001, 0): FMatrix * FVector with the zeros folded schedules its three
// products among the emitter call's pushes as retail does (0044ac3d).
void CEscapePod::Move()	// FUNCTION: 0x0044ac20
{
	mVelocity += mOrientation * FVector(0, 0.001f, 0);

	FVector	pos;
	FMatrix	ori;
	mRenderThing->GetRTEmitter("Engine", 1, pos, ori, FALSE, FALSE);
	mTrail.SetPos(pos);
	mTrail.SetOrientation(ori);
	SUPERTYPE::Move();
}

//******************************************************************************************
// Pitch from Row[2].Y, as FMatrix(yaw, pitch, roll) builds it; at a pole the yaw and roll are taken as zero.
CEulerAngles::CEulerAngles(const FMatrix &m)	// FUNCTION: 0x0044adb0
{
	mPitch = (float)asin(m.Row[2].Y);
	if ((mPitch < PI) && (mPitch > -PI))
	{
		mYaw = (float)atan2(-m.Row[0].Y, m.Row[1].Y);
		mRoll = (float)atan2(-m.Row[2].X, m.Row[2].Z);
	}
	else
	{
		mYaw = 0;
		mRoll = 0;
	}
}
