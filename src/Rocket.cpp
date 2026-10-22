// Rocket.cpp: reconstructed from Rocket.obj (004d7a20..004d80e8); Stuart's release has no copy. A rocket on its pad:
// once active its four engines burn and it climbs; enough damage, or its shutdown, sets off the big explosion.

#include "common.h"
#include "Rocket.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "eventmanager.h"

// FUNCTION: 0x004d7a20 init:ZERO_FVECTOR
// FUNCTION: 0x004d7a40 init:ID_FMATRIX

//******************************************************************************************
void CRocket::Init(CInitThing *init)	// FUNCTION: 0x004d7b10
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY | THING_TYPE_BUILDING;

	CRenderData	data;
	data.mInit[0].mThing = this;
	data.mRTID[0] = 1;
	strcpy(data.mInit[0].mName, "m_rocket.msh");

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	init->mForceRadius = GetRadius();
	mActive = init->mActive;
	if (mActive)
		Activate();
	mLife = 25.0f;
	mVulnerable = TRUE;
	SUPERTYPE::Init(init);
}

//******************************************************************************************
void CRocket::Shutdown()	// FUNCTION: 0x004d7c60
{
	for (SINT i = 0; i < 4; i++)
		mEngines[i].Kill();
	SUPERTYPE::Shutdown();
}

//******************************************************************************************
// Climbs a little faster each frame, the engines following their emitters.
void CRocket::Move()	// FUNCTION: 0x004d7c90
{
	if (mActive)
	{
		if (mVelocity.Z <= GetMaxVelocity() / 20.0f - 0.001f)
			mVelocity.Z -= 0.001f;

		for (SINT i = 0; i < 4; i++)
		{
			FVector	pos;
			FMatrix	ori;
			mRenderThing->GetRTEmitter("Engine", i + 1, pos, ori, FALSE, FALSE);
			mEngines[i].SetPos(pos);
			mEngines[i].SetOrientation(ori);
		}
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
void CRocket::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x004d7e30
{
	if (mVulnerable)
	{
		mLife -= amount;
		if (mLife < 0)
			AddShutdownEvent();
	}
}

//******************************************************************************************
void CRocket::AddShutdownEvent()	// FUNCTION: 0x004d7e60
{
	if (mFlags & TF_DECLARED_SHUTDOWN)
		return;
	SUPERTYPE::AddShutdownEvent();

	CExplosion	*explosion = UPhysicsManager::SpawnExplosion("Big Rocket Explosion");
	CExplosionInitThing	init;
	init.mBehaviour = UPhysicsManager::GetExplosion("Big Rocket Explosion");
	init.mColType = kCollideNothing;
	init.mAttachedTo = this;
	init.mAllegiance = kNeutralAllegiance;
	init.mUseAttachedRadius = TRUE;
	if (explosion)
	{
		init.mPos = mPos;
		explosion->Init(&init);
	}
}

//******************************************************************************************
void CRocket::Activate()	// FUNCTION: 0x004d8040
{
	mActive = TRUE;
	CParticleDescriptor	*pd = PARTICLE_SET.GetPD("Big Rocket Engine Effect");
	if (pd)
		for (SINT i = 0; i < 4; i++)
			PARTICLE_MANAGER.CreateEffect(pd, &mEngines[i], ZERO_FVECTOR, 0, 0);
}

//******************************************************************************************
void CRocket::Deactivate()	// FUNCTION: 0x004d80c0
{
	mActive = FALSE;
	for (SINT i = 0; i < 4; i++)
		mEngines[i].Kill();
}
