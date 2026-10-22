// Boat.cpp: reconstructed from Boat.obj (00414d60..00415a9f); Stuart's release has no copy. A landing boat: it rides
// the water, and once its bow ("FootBase") has run aground for long enough it lands, deploys and unloads.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "BoatAI.h"
#include "BoatGuide.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "map.h"
#include "eventmanager.h"
#include "event.h"
#include "WorldPhysicsManager.h"
#include "DebugLog.h"
#include "explosion.h"
#include "mesh.h"
#include "game.h"

// FUNCTION: 0x00414d60 init:ZERO_FVECTOR
// FUNCTION: 0x00414d80 init:ID_FMATRIX

// The position of a mesh emitter, as AirUnit.cpp has it.
static inline FVector EmitterPos(CRenderThing *rt, char *name, SINT index)
{
	FVector	pos;
	FMatrix	ori[1];
	rt->GetRTEmitter(name, index, pos, ori[0], TRUE, FALSE);
	return pos;
}

//******************************************************************************************
void CBoat::Init(CInitThing *init)	// FUNCTION: 0x00414e50
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_TREE | THING_TYPE_CAN_BE_WALKED_ON;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mIgnoreAnimationCollision = TRUE;

	SUPERTYPE::Init(init);

	float	turn = ((CUnitInitThing *)init)->mStats->mTurnRate;
	mUnknown12C = CEulerAngles(turn, 0.0015f, turn);
	mMaxShields = mShields = ((CUnitInitThing *)init)->mStats->mGroundShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mGroundShieldRegeneration;

#line 30
	mGuide = new(MEMTYPE_GUIDE) CBoatGuide(this);
	mAI = new(MEMTYPE_AI) CBoatAI(this, init);
	mDeploying = FALSE;
	mLanded = FALSE;
	mBeachCount = 0;
}

// The AIs' implicit destructors, emitted here with their vtables.
// FUNCTION: 0x00414fa0 ??_GCBoatAI@@UAEPAXI@Z
// FUNCTION: 0x00414fc0 ??1CBoatAI@@UAE@XZ
// FUNCTION: 0x00415080 ??1CUnitAI@@UAE@XZ

//******************************************************************************************
// Event 8000: it has run aground.
void CBoat::HandleEvent(CEvent *event)	// FUNCTION: 0x00415120
{
	if (event->GetEventNum() != 8000)
		SUPERTYPE::HandleEvent(event);
	else
		Unknown93();
}

//******************************************************************************************
// It lands: it stops, and deploys if it is active.
void CBoat::Unknown93()	// FUNCTION: 0x00415140
{
	if (mLanded != TRUE)
	{
		LOG.AddMessage("landed !!!!!!!!!!!!");
		mUnknown12C.mYaw = 0;
		DeclareOnGround();
		Stop();
		if (mActive)
			Unknown82();
		mLanded = TRUE;
	}
}

//******************************************************************************************
// The final velocity is a named local because retail gives it a frame slot of its own (the frame is 0x70 bytes); an unnamed
// temporary reuses the slot `pos` had, and the function is then 22 instructions off.
void CBoat::Move()	// FUNCTION: 0x004151a0
{
	if (!mLanded)
		DeclareInWater();
	else
		DeclareOnGround();

	SUPERTYPE::Move();

	// a dying boat sinks
	if ((mFlags & TF_DYING) && (!mUnitData->mUnknown11C))
	{
		mUnknown120.mPitch = PI * 2 / 7;
		mVelocity.Z = GetMoveMultiplier() * 0.05f;
		for (SINT n = (SINT)GetMoveMultiplier(); n > 0; n--)
			Unknown96();
		return;
	}

	// its bow runs aground
	FVector	bow = EmitterPos(mRenderThing, "FootBase", 1);
	if (bow != FVector(0, 0, 0))
	{
		FVector	pos = mOrientation * bow + mPos;
		if (MAP.Collide(pos) < pos.Z)
		{
			mBeachCount++;
			EVENT_MANAGER.AddEvent(8000, this, EVENT_MANAGER.GetTime() + 3.0f, 0, NULL, NULL);
			CEulerAngles	e(mOrientation);
			e.mPitch -= 0.01f;
			mCurrentOrientation = e;
			mUnknown120.mPitch = e.mPitch;
			mOrientation = FMatrix(e.mYaw, e.mPitch, e.mRoll);
			if (mBeachCount > 60)
				Unknown93();
		}
	}

	mPos.Z = MAP.GetWaterLevel() + COfGHeight();

	float	speed = GetVelocity().MagnitudeXY();
	FVector	vel(speed * mOrientation.Row[0].Y, speed * mOrientation.Row[1].Y, speed * mOrientation.Row[2].Y);
	SetVelocity(vel);
}

//******************************************************************************************
BOOL CBoat::CanGoUnderWater()	// FUNCTION: 0x00415430
{
	if ((mFlags & TF_DYING) && (!mUnitData->mUnknown11C))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
// Now and then, in the water, a small explosion at a random point about its hull: towards the ends, either side,
// and never below the waterline.
// STAND-IN (spelling, no code): the z offset's brackets keep it a value (README: parentheses are a value barrier): retail's
// order of the position's stores and its pop of ebx before the null test; without them the function is 21 bytes (2
// instructions) off.
void CBoat::Unknown96()	// FUNCTION: 0x00415450
{
	if (!mUnitData)
		return;
	if ((GAME.FloatRandom() > 0.7f) && (IsInWater()))
	{
		CExplosionInitThing	init;
		float	x = (GAME.FloatRandom() * 0.25f + 0.75f) * GetBoundingBox()->mAxes.X;
		float	sx = (GAME.FloatRandom() > 0.5f) ? x : -x;
		float	y = GAME.FloatRandom() * GetBoundingBox()->mAxes.Y * 1.5f - GetBoundingBox()->mAxes.Y * 0.75f;
		float	z = (GAME.FloatRandom() * GetBoundingBox()->mAxes.Z - GetBoundingBox()->mAxes.Z * 0.5f);	// STAND-IN (spelling, no code), see above
		FVector	local = FVector(sx, y, z) + GetBoundingBox()->mOrigin;
		init.mPos = mOrientation * local + mPos;

		SINT		index = mUnitData->mSmallExplosion;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
		if (explosion)
		{
			init.mAllegiance = (EAllegiance)mAllegiance;
			init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
			init.mColType = kCollideWater;
			if (MAP.GetWaterLevel() > init.mPos.Z)
				init.mPos.Z = MAP.GetWaterLevel();
			init.mPos.Z -= 0.15f;
			explosion->Init(&init);
		}
	}
}

//******************************************************************************************
void CBoat::Unknown82()	// FUNCTION: 0x00415780
{
	if (!mDeploying)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("deploying"), TRUE, FALSE);
		mDeploying = TRUE;
	}
}

//******************************************************************************************
void CBoat::Unknown83()	// FUNCTION: 0x004157c0
{
	mUnknown1F0 = 0;
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("undeploying"), TRUE, FALSE);
}

//******************************************************************************************
// Aim at the centre of its first part's box, as the hull sits now.
void CBoat::GetTargetablePos(FVector *out)	// FUNCTION: 0x00415800
{
	CMesh	*mesh;
	if ((mRenderThing) && (mesh = mRenderThing->GetRTMesh()))
	{
		CMeshPart	*part = mesh->mParts[0];
		FVector		pos = part->mOffsetPos;
		pos += part->mOrientation * part->mBoundingBox->mOrigin;
		*out = GetRenderOrientation() * pos + mPos;
	}
	else
		GetCentrePos(*out);
}

//******************************************************************************************
BOOL CBoat::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00415970
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("deploying"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("deployed"), TRUE, TRUE);
			mUnknown1F0 = 1;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("undeploying"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("normal"), TRUE, TRUE);
			mDeploying = FALSE;
			return FALSE;
		}
		return SUPERTYPE::FinishedPlayingCurrentAnimation();
	}
	return TRUE;
}

//******************************************************************************************
BOOL CBoat::Unknown76()	// FUNCTION: 0x00415a50
{
	if (IsOnGround())
		return FALSE;
	return SUPERTYPE::Unknown76();
}

//******************************************************************************************
// IsDying's if/else inlines to the byte test retail has (movsx, and 4, shr 2).
BOOL CBoat::ClipToGround()	// FUNCTION: 0x00415a90
{
	return IsDying();
}

//******************************************************************************************
// The boat's answer (as CFeature's, feature.cpp) that no code above defined. It compiles to CFeature's body, so the
// linker folds it into that one (/OPT:ICF); like CFeature's SUPERTYPE call, it calls CActor's directly.

// FUNCTION: 0x0044d1f0
void CBoat::DeclareOnGround()
{
	CActor::DeclareOnGround();
	if (mFlags & TF_DYING)
		AddShutdownEvent();
}
