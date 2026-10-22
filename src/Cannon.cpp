// Cannon.cpp: reconstructed from Cannon.obj (0041b0b0..0041b5ab); Stuart's release has no copy. A cannon: a fixed
// emplacement that plays its "Activate" animation when its AI finds a target and "Deactivate" ten seconds after it
// last had one.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#define CANNON_CPP
#include "unit.h"
#include "Motioncontroller.h"
#include "MCCannon.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "Guide.h"
#include "rtmesh.h"
#include "mesh.h"
#include "map.h"
#include "world.h"
#include "eventmanager.h"

// FUNCTION: 0x0041b0b0 init:ZERO_FVECTOR
// FUNCTION: 0x0041b0d0 init:ID_FMATRIX

//******************************************************************************************
void CCannon::Init(CInitThing *init)	// FUNCTION: 0x0041b1a0
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;
	SUPERTYPE::Init(init);

	if (mActive)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Active"), TRUE, TRUE);
		mState = 0;
	}
	else
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Inactive"), TRUE, TRUE);
		mState = 1;
	}

#line 34
	mGuide = new(MEMTYPE_GUIDE) CTerrainGuide(this);
	mAI = new(MEMTYPE_AI) CUnitAI(this, init);
	mUnknown12C = CEulerAngles(0, 0, 0);
#line 38
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCCannon(this);
	mUnknownF4 = 0;
	mLastTargetTime = 0;
	WORLD.AddToOccupancy(this);
	if (MAP.GetWaterLevel() > mPos.Z)
		mAboveWater = FALSE;
	else
		mAboveWater = TRUE;
}

//******************************************************************************************
void CCannon::Move()	// FUNCTION: 0x0041b370
{
	if ((mActive) && (mAI->mTarget.ToRead()))
	{
		if ((mState != 0) && (mState != 3))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Activate"), TRUE, FALSE);
			mState = 3;
		}
		if (mAI->mTarget.ToRead())
			mLastTargetTime = EVENT_MANAGER.GetTime();
	}
	else if (mLastTargetTime + 10.0f < EVENT_MANAGER.GetTime())
	{
		if ((mState != 1) && (mState != 2))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Deactivate"), TRUE, FALSE);
			mState = 2;
		}
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
// CSentinel's is the same code; the linker keeps this copy.
void CCannon::Shutdown()	// FUNCTION: 0x0041b450
{
	WORLD.RemoveFromOccupancy(this);
	CUnit::Shutdown();
}

//******************************************************************************************
// After "Activate" it stays "Active"; after "Deactivate", "Inactive". The else-if (one shared return FALSE) is what
// lets retail save ebx only after the first test (0041b48b).
BOOL CCannon::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x0041b470
{
	if (GetRenderAnimation() != -1)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("Activate"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Active"), TRUE, TRUE);
			mState = 0;
		}
		else if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("Deactivate"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Inactive"), TRUE, TRUE);
			mState = 1;
		}
		return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
// Halfway between its position and where it is aimed at.
void CCannon::GetTargetablePos(FVector *out)	// FUNCTION: 0x0041b540
{
	CUnit::GetTargetablePos(out);
	*out += mPos;
	*out *= 0.5f;
}

//******************************************************************************************
BOOL CCannon::StartDieProcess()	// FUNCTION: 0x0041b590
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	Remove();
	return TRUE;
}

//******************************************************************************************
// The cannon's answers that no code above defined. Each compiles to a body another function already has, so the linker
// folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each annotation
// names (the class header notes the slots' survivors too).
