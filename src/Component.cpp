// Component.cpp: reconstructed from Component.obj (00427a90..00428d8f); Stuart's release has no copy. A unit that is
// part of another (a turret, a gun, a head), mounted at one of its parent's emitters.

#include "common.h"
#include "component.h"
#include "mesh.h"
#include "DebugLog.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "game.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "map.h"
#include "Motioncontroller.h"
#include "Monitor.h"
#include "Guide.h"
#include "MCComponent.h"

// FUNCTION: 0x00427a90 init:ZERO_FVECTOR
// FUNCTION: 0x00427ab0 init:ID_FMATRIX
// The AIs' implicit destructors, emitted here with their vtables (CRepairPadAI's are 00417480, elsewhere).
// FUNCTION: 0x00427f90 ??_GCComponentBomberAI@@UAEPAXI@Z
// FUNCTION: 0x00427fb0 ??1CComponentBomberAI@@UAE@XZ
// FUNCTION: 0x00428050 ??_GCFenrirMainGunAI@@UAEPAXI@Z
// FUNCTION: 0x00428070 ??1CFenrirMainGunAI@@UAE@XZ

//******************************************************************************************
void CComponent::Init(CInitThing *init)	// FUNCTION: 0x00427b80
{
	mParent = NULL;
	mNumber = 0;
	mUnknown250 = 0;
	mUnknown254 = 0;
	Unknown117();

	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_UNKNOWN_40000000 | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_STATIONARY;
	CUnitData	*data = ((CUnitInitThing *)init)->mStats;
	if ((data) && (!strcmp("Thunderhead Main Gun", data->mName)))
	{
		init->mInitCST.mMinCollision = ECL_OUTER_SPHERE;
		init->mInitCST.mMaxCollision = ECL_MESH;
		init->mInitCST.mDesiredCollision = ECL_APPROX_GEOMETRY_SHAPES;
	}

	SUPERTYPE::Init(init);

	if (mActive)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Activated"), TRUE, TRUE);
		mUnknown264 = FALSE;
	}
	else
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Normal"), TRUE, TRUE);
		mUnknown264 = TRUE;
	}

	Unknown119(init);
	Unknown118();

	mUnknown278 = 99999;
	mUnknownF4 = 0;
	mUnknown268 = 0;
	mUnknown260 = 0;
	mYawOffset = 0;
	mUnknown2BC = FALSE;
}

//******************************************************************************************
void CComponent::Unknown117()	// FUNCTION: 0x00427cd0
{
#line 77
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCComponent(this);
}

//******************************************************************************************
void CComponent::Unknown118()	// FUNCTION: 0x00427d50
{
#line 83
	mGuide = new(MEMTYPE_GUIDE) CComponentGuide(this);
}

//******************************************************************************************
// The AI: three components have their own.
void CComponent::Unknown119(CInitThing *init)	// FUNCTION: 0x00427dd0
{
	if (!stricmp(mUnitData->mName, "Fenrir Bomb Launcher"))
#line 92
		mAI = new(MEMTYPE_AI) CComponentBomberAI(this, init);
	else if (!stricmp(mUnitData->mName, "Fenrir Main Gun"))
#line 94
		mAI = new(MEMTYPE_AI) CFenrirMainGunAI(this, init);
	else if (!stricmp(mUnitData->mName, "Carrier Health Pad"))
#line 96
		mAI = new(MEMTYPE_AI) CRepairPadAI(this, init);
	else
	{
#line 99
		mAI = new(MEMTYPE_AI) CUnitAI(this, init);
		mAI->mUnknown14 = 0;
	}
}

//******************************************************************************************
void CComponent::Move()	// FUNCTION: 0x00428110
{
	// falling away while dying
	if ((mFlags & TF_DYING) && (mUnitData->mUnknown198))
	{
		mUnknown12C.mPitch = 0.005f;
		mUnknown120.mPitch = mCurrentOrientation.mPitch - 0.8f;
		mVelocity.Z += Gravity();
		mVelocity *= Unknown73();
		if (IsInWater())
		{
			mVelocity *= 0.98f;
			mUnknown12C.mPitch = 0;
		}
		for (SINT i = 0; i < 2; i++)
			Unknown95();
		if (-mUnitData->mUnknownC0 > mLife)
		{
			SpawnExplosion();
			AddShutdownEvent();
		}
	}

	// open up while it has a target; close a while after it loses one
	if ((mActive) && (mAI->mTarget.ToRead()))
	{
		if ((mUnknown264 != 0) && (mUnknown264 != 3))
		{
			EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("Activate");
			if (anim != AM_INVALID)
			{
				SetAnimMode(anim, TRUE, FALSE);
				mUnknown264 = 3;
			}
		}
		if (mAI->mTarget.ToRead())
			mUnknown268 = EVENT_MANAGER.GetTime();
	}
	else if ((mUnknown268 + 2.0f < EVENT_MANAGER.GetTime()) && (mUnknown264 != 1) && (mUnknown264 != 2))
	{
		EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("Deactivated");
		if (anim != AM_INVALID)
		{
			SetAnimMode(anim, TRUE, FALSE);
			mUnknown264 = 2;
		}
	}

	// the Gill-M's claw strikes
	if (mUnknown2BC)
	{
		CExplosionInitThing	init;
		SUPERTYPE::GetEmitter(1, 1, init.mPos, init.mOrientation);
		init.mAllegiance = (EAllegiance)mAllegiance;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion("Gill-M Claw Hit");
		if (explosion)
		{
			init.mBehaviour = UPhysicsManager::GetExplosion("Gill-M Claw Hit");
			init.mColType = kCollideNothing;
			if (MAP.GetWaterLevel() < init.mPos.Z)
				init.mPos.Z = MAP.GetWaterLevel();
			explosion->Init(&init);
		}
	}

	SUPERTYPE::Move();

	if (GAME.FloatRandom() > 0.9f)
		RefreshCachedTransform();
}

//******************************************************************************************
float CComponent::Unknown92()	// FUNCTION: 0x004284f0
{
	return -(float)atan2(mOrientation.Row[0].Y, mOrientation.Row[1].Y);
}

//******************************************************************************************
// Its place on its parent's "Component" emitter, turned by its own yaw and pitch; once a frame.
void CComponent::RefreshCachedTransform()	// FUNCTION: 0x00428500
{
	if (mUnknown278 == GAME.GetRenderFrameNumber())
		return;

	if (!((mFlags & TF_DYING) && (mUnitData->mUnknown198)))
	{
		FMatrix	turn(mUnknown250, mUnknown254, 0);
		FMatrix	ori;
		if (mParent.ToRead())
			mParent->GetRenderThing()->GetRTEmitter("Component", mNumber, mPos, ori, FALSE, FALSE);
		mOrientation = turn * ori;
	}
	mUnknown278 = GAME.GetRenderFrameNumber();
}

//******************************************************************************************
FVector CComponent::GetRenderPos()	// FUNCTION: 0x00428710
{
	if ((mFlags & TF_DYING) && (mUnitData->mUnknown198))
		return SUPERTYPE::GetRenderPos();
	RefreshCachedTransform();
	return mPos;
}

//******************************************************************************************
FMatrix CComponent::GetRenderOrientation()	// FUNCTION: 0x00428770
{
	if ((mFlags & TF_DYING) && (mUnitData->mUnknown198))
		return SUPERTYPE::GetRenderOrientation();
	RefreshCachedTransform();
	return mOrientation;
}

//******************************************************************************************
FVector CComponent::GetVelocity()	// FUNCTION: 0x004287c0
{
	if (mParent.ToRead())
		return mParent->GetVelocity();
	return mVelocity;
}

//******************************************************************************************
// Dying: one that clips to the ground falls away from its parent, another explodes after seven seconds.
BOOL CComponent::StartDieProcess()	// FUNCTION: 0x00428800
{
	if ((!mUnitData->mUnknown124) && (!mUnitData->mUnknown198))
	{
		if (SUPERTYPE::StartDieProcess())
		{
			Remove();
			return TRUE;
		}
		return FALSE;
	}

	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	ReleaseChildren();

	if (mUnitData->mUnknown198)
	{
		FVector	away = mPos - mParent->GetPos();
		away.Z = 0;
		away.Normalise();
		SetVelocity(mParent->GetVelocity() + away * 0.4f + mParent->GetOrientation() * FVector(0, 0.4f, 0));
	}
	else
		EVENT_MANAGER.AddEvent(4004, this, EVENT_MANAGER.GetTime() + 7.0f);
	return TRUE;
}

//******************************************************************************************
// hit -> retract -> normal; Activate -> Activated; Deactivated -> Normal.
BOOL CComponent::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x004289b0
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("hit"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("retract"), TRUE, FALSE);
			mUnknown2BC = FALSE;
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("retract"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("normal"), TRUE, TRUE);
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("Activate"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Activated"), TRUE, TRUE);
			mUnknown264 = FALSE;
			mUnknown268 = EVENT_MANAGER.GetTime();
			SUPERTYPE::Activate();
			return FALSE;
		}
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("Deactivated"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Normal"), TRUE, TRUE);
			mUnknown264 = TRUE;
			mUnknown268 = EVENT_MANAGER.GetTime() + 2.0f;
			SUPERTYPE::Deactivate();
			return FALSE;
		}
		return SUPERTYPE::FinishedPlayingCurrentAnimation();
	}
	return TRUE;
}

//******************************************************************************************
void CComponent::Unknown428b50(CUnit *owner, SINT mount)	// FUNCTION: 0x00428b50
{
	mParent = owner;
	mNumber = mount;
	if (owner)
	{
		FVector	pos;
		FMatrix	ori;
		owner->GetEmitter(20, mount, pos, ori);
		mYawOffset = -(float)atan2(ori.Row[0].Y, ori.Row[1].Y) - owner->mCurrentOrientation.mYaw;
		if (owner->IsA(THING_TYPE_CAN_BE_WALKED_ON))
			GetThingType() |= THING_TYPE_CAN_BE_WALKED_ON;
	}
}

//******************************************************************************************
float CComponent::GetMountedYaw()	// FUNCTION: 0x00428bc0
{
	if (mParent.ToRead())
		return mParent->mCurrentOrientation.mYaw + mYawOffset;
	return 0;
}

//******************************************************************************************
// A component only turns by itself once it is dying (it follows its parent until then).
void CComponent::Unknown77(CEulerAngles *cur, CEulerAngles *target, CEulerAngles *rate, FMatrix &out)	// FUNCTION: 0x00428be0
{
	if (mFlags & TF_DYING)
	{
		if (*cur != *target)
			SUPERTYPE::Unknown77(cur, target, rate, out);
	}
}

//******************************************************************************************
float CComponent::Unknown73()	// FUNCTION: 0x00428c30
{
	return 0.999f;
}

//******************************************************************************************
float CComponent::Gravity()	// FUNCTION: 0x00428c40
{
	return 0.005f;
}

//******************************************************************************************
BOOL CComponent::ClipToGround()	// FUNCTION: 0x00428c50
{
	return mUnitData->mUnknown198 != 0;
}

//******************************************************************************************
void CComponent::DeclareInWater()	// FUNCTION: 0x00428c70
{
	SUPERTYPE::DeclareInWater();
	if (mFlags & TF_DYING)
		AddShutdownEvent();
}

//******************************************************************************************
BOOL CComponent::Unknown84()	// FUNCTION: 0x00428c90
{
	if (mUnknown264)
		return FALSE;
	return SUPERTYPE::Unknown84();
}

//******************************************************************************************
void CComponent::PlayHit()	// FUNCTION: 0x00428cb0
{
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("hit"), TRUE, FALSE);
	mUnknown2BC = TRUE;
}

//******************************************************************************************
void CComponent::Unknown107(float a)	// FUNCTION: 0x00428cf0
{
	if ((mUnitData->mUnknown134) && (mParent.ToRead()))
		mParent->Unknown107(a);
	SUPERTYPE::Unknown107(a);
}

//******************************************************************************************
void CComponent::GetTargetablePos(FVector *out)	// FUNCTION: 0x00428d30
{
	*out = mPos;
}

//******************************************************************************************
void CComponent::Activate()	// FUNCTION: 0x00428d50
{
	EAnimMode	anim = mRenderThing->GetRTMesh()->GetAnimModeByName("Activate");
	if (anim == AM_INVALID)
	{
		mUnknown264 = FALSE;
		SUPERTYPE::Activate();
	}
	else
		SetAnimMode(anim, TRUE, FALSE);
}

// FUNCTION: 0x0044d1f0
void CComponent::DeclareOnGround()
{
	CActor::DeclareOnGround();		// as CFeature's SUPERTYPE call (0044d1f0 -> 00402000)
	if (mFlags & TF_DYING)
		AddShutdownEvent();
}

//******************************************************************************************
// The component's answers (as CSimpleBuilding's and CFeature's, SimpleBuilding.cpp and feature.cpp) that no code above
// defined. Each compiles to a body another function already has, so the linker folds it into that one (/OPT:ICF): the
// retail table's slot points at the survivor, whose address each annotation names (the class header notes the slots'
// survivors too).

// FUNCTION: 0x004dfcb0
BOOL CComponent::CanGoUnderWater()
{
	if (IsDying())
		return FALSE;
	return TRUE;
}
