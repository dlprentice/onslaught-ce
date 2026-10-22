// feature.cpp: CFeature, a piece of scenery that can be damaged and destroyed (reconstructed; Stuart's file is not
// in his release). A destroyed feature topples, may throw out a pickup as it goes, and is gone once it sinks.

#include	"Common.h"

#define FEATURE_CPP
#include	"feature.h"
#include	"InitThing.h"
#include	"RenderThing.h"
#include	"SoundManager.h"
#include	"staticshadows.h"
#include	"world.h"
#include	"map.h"
#include	"explosion.h"
#include	"MissionScript\IScript.h"
#include	"game.h"
#include	"mesh.h"

// The per-file maths constants from maths.h come first:
// FUNCTION: 0x0044c940 init:ZERO_FVECTOR
// FUNCTION: 0x0044c960 init:ID_FMATRIX

//*********************************************************************************
void	CFeature::Init(CInitThing *init)	// FUNCTION: 0x0044ca30
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_UNKNOWN_20;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	mData = ((CFeatureInitThing *)init)->mData;

	CRenderData	data;
	data.mInit[0].mThing = this;
	data.mRTID[0] = 1;
	if (mData->mMeshName)
		strcpy(data.mInit[0].mName, mData->mMeshName);

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	SUPERTYPE::Init(init);

	mStaticShadow = NULL;
	mAllegiance = init->mAllegiance;
	mLife = mData->mLife;
	WORLD.AddToOccupancy((CUnit *)this);

	if (MAP.GetWaterLevel() > mPos.Z)
		mAboveWater = TRUE;
	else
		mAboveWater = FALSE;

	if (mData->mSound)
		SOUND.PlayEffect(mData->mSound, this, 0.7f, ST_FOLLOWANDDIE, true, 0, 0, -1.f, true, 1.f, ST_GAME_SOUND, 0);
}

//*********************************************************************************
void	CFeature::Shutdown()	// FUNCTION: 0x0044cbe0
{
	SOUND.KillSamplesForThing(this);
	WORLD.RemoveFromOccupancy((CUnit *)this);
	STATICSHADOWS.UpdateVisibility(this, TRUE);
	SUPERTYPE::Shutdown();
}

//*********************************************************************************
// a dying feature topples over, then sinks away
void	CFeature::Move()	// FUNCTION: 0x0044cc10
{
	SUPERTYPE::Move();

	if ((mFlags & TF_DYING) && (!mAboveWater))
	{
		mOldOrientation = mOrientation;

		FVector	up = mOrientation.GetColumnY();
		float	m = sqrtf(up.X * up.X + up.Y * up.Y + up.Z * up.Z);
		float	pitch;
		if (m > 0)
			pitch = (float)asin(up.Z / m);
		else
			pitch = 0;

		float	yaw = mOrientation.GetColumnY().Azimuth();
		pitch += 0.0015f;
		mOrientation = FMatrix(yaw, pitch, 0);

		SetVelocity(FVector(0, 0, 0.04f));
		SpawnPickup();

		if (GetPos().Z > 25.f)
			AddShutdownEvent();
	}
}

//*********************************************************************************
void	CFeature::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x0044cd20
{
	if (!mData->mInvincible)
	{
		mLife -= amount;
		if ((mLife < 0) && (!(mFlags & TF_DYING)))
			StartDieProcess();
		if (mLife > mData->mLife)
			mLife = mData->mLife;
	}
}

//*********************************************************************************
BOOL	CFeature::StartDieProcess()	// FUNCTION: 0x0044cd80
{
	if (mFlags & TF_DYING)
		return FALSE;

	mFlags |= TF_DYING;
	if (mMissionScript)
		mMissionScript->StartedDying();
	return TRUE;
}

//*********************************************************************************
void	CFeature::AddShutdownEvent()	// FUNCTION: 0x0044cdb0
{
	if (mFlags & TF_DECLARED_SHUTDOWN)
		return;

	SUPERTYPE::AddShutdownEvent();

	if (mData->mExplosion)
	{
		if (CExplosion *explosion = UPhysicsManager::SpawnExplosion(mData->mExplosion))
		{
			CExplosionInitThing	init;
			init.mBehaviour = UPhysicsManager::GetExplosionData(mData->mExplosion);
			init.mAttachedTo = this;
			init.mUseAttachedRadius = TRUE;
			init.mPos = mPos;
			init.mAllegiance = (EAllegiance)mAllegiance;
			explosion->Init(&init);
		}
	}
}

//*********************************************************************************
// now and then, as it topples, its explosion goes off again somewhere on it
void	CFeature::SpawnPickup()	// FUNCTION: 0x0044cee0
{
	if (!mData)
		return;

	if (float(GAME.Random() % 65536) * (1.0f / 65536.0f) > 0.75f)
	{
		CExplosionInitThing	init;

		float	x = (GAME.FloatRandom() * GetBoundingBox()->mAxes.X) - GetBoundingBox()->mAxes.X * 0.5f;
		float	y = (GAME.FloatRandom() * GetBoundingBox()->mAxes.Y) - GetBoundingBox()->mAxes.Y * 0.5f;
		float	z = (GAME.FloatRandom() * GetBoundingBox()->mAxes.Z) - GetBoundingBox()->mAxes.Z * 0.5f;
		FVector	offset = FVector(x, y, z) + GetBoundingBox()->mOrigin;
		init.mPos = mOrientation * offset + mPos;
		init.mAllegiance = (EAllegiance)mAllegiance;

		if (mRenderThing)
			init.mPos = mRenderThing->GetRandomSurfacePoint(0);

		SINT	index = mData->mExplosion;
		if (CExplosion *explosion = UPhysicsManager::SpawnExplosion(index))
		{
			init.mBehaviour = UPhysicsManager::GetExplosionData(mData->mExplosion);
			init.mColType = kCollideWater;
			explosion->Init(&init);
		}
	}
}

//*********************************************************************************
BOOL	CFeature::ClipToGround()	// FUNCTION: 0x0044d1d0
{
	if ((mFlags & TF_DYING) && (!mAboveWater))
		return FALSE;
	return TRUE;
}

//*********************************************************************************
void	CFeature::DeclareOnGround()	// FUNCTION: 0x0044d1f0
{
	SUPERTYPE::DeclareOnGround();
	if (mFlags & TF_DYING)
		AddShutdownEvent();
}

//*********************************************************************************
void	CFeature::Render(DWORD flags)	// FUNCTION: 0x0044d210
{
	STATICSHADOWS.UpdateVisibility(this, FALSE);
	CThing::Render(flags);
}

//******************************************************************************************
// A feature goes under water while it dies. It compiles to CBoat::ClipToGround's body, so the linker folds it into
// that one (/OPT:ICF).

// FUNCTION: 0x00415a90
BOOL CFeature::CanGoUnderWater()
{
	return IsDying();
}
