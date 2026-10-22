// DiveBomber.cpp: reconstructed from DiveBomber.obj (00445160..004456d5); Stuart's release has no copy. The dive
// bomber: a small flier that bobs as it flies and opens its bay to shoot. The allocations' __LINE__s are the retail
// ones; the #line places them.

#include "common.h"
class CDiveBomberGuide;class CDiveBomberAI;class CUnitAI;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "unit.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "mesh.h"
#include "game.h"
#include "DiveBomberAI.h"			// before the guide: retail emits the AI's inline members (00445380) before the guide's (00445440)
#include "DiveBomberGuide.h"
// FUNCTION: 0x00445160 init:ZERO_FVECTOR
// FUNCTION: 0x00445180 init:ID_FMATRIX

//******************************************************************************************
void CDiveBomber::Init(CInitThing *init)	// FUNCTION: 0x00445250
{
	SUPERTYPE::Init(init);

#line 18
	mAI = new(MEMTYPE_AI) CDiveBomberAI(this, init);
	mGuide = new(MEMTYPE_GUIDE) CDiveBomberGuide(this);

	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("fly"), TRUE, TRUE);
	mBayState = 1;
	mBob = 0;
	mPhase = GAME.FloatRandom() * PI_M2;
}

// The AI's and guide's implicit destructors, emitted here with their vtables.
// FUNCTION: 0x00445380 ??_GCDiveBomberAI@@UAEPAXI@Z
// FUNCTION: 0x004453a0 ??1CDiveBomberAI@@UAE@XZ
// FUNCTION: 0x00445440 ??_GCDiveBomberGuide@@UAEPAXI@Z
// FUNCTION: 0x00445460 ??1CDiveBomberGuide@@UAE@XZ

//******************************************************************************************
void CDiveBomber::Move()	// FUNCTION: 0x004454c0
{
	if (mUnknown168 == 1)
	{
		mUnknown168 = 0;
		mUnknown1E8 = 1;
	}
	if (mFlags & TF_DYING)
	{
		float	roll = mCurrentOrientation.mRoll + 0.8f;
		mUnknown12C.mRoll = 0.2f;
		if (roll > PI)
			roll -= PI_M2;
		mUnknown120.mRoll = roll;
		mUnknown120.mPitch = PI / 2;
	}
	else
	{
		mPhase += 0.1f;
		mBob = (float)sin(mPhase) * 0.2f;
		if (mPhase > PI_M2)
			mPhase -= PI_M2;
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
void CDiveBomber::OpenBay()	// FUNCTION: 0x00445570
{
	if ((mBayState == 1) || (mBayState == 3))
	{
		mBayState = 2;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("open"), TRUE, FALSE);
	}
}

//******************************************************************************************
void CDiveBomber::CloseBay()	// FUNCTION: 0x004455c0
{
	if ((mBayState == 0) || (mBayState == 2))
	{
		mBayState = 3;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("close"), TRUE, FALSE);
	}
}

//******************************************************************************************
// The else-if (one shared return FALSE) is what lets retail save EBX only inside the block (0044562b).
BOOL CDiveBomber::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00445610
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("open"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("shoot"), TRUE, TRUE);
			mBayState = 0;
		}
		else if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("close"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("fly"), TRUE, TRUE);
			mBayState = 1;
		}
		return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
// The dive bomber's answers (as CPlane's and CGroundAttackAircraft's, Plane.cpp and GroundAttackAircraft.cpp) that no
// code above defined. Each compiles to a body another function already has, so the linker folds it into that one
// (/OPT:ICF): the retail table's slot points at the survivor, whose address each annotation names (the class header
// notes the slots' survivors too).

// FUNCTION: 0x004d20a0
void CDiveBomber::DeclareOnGround()
{
	SUPERTYPE::DeclareOnGround();
	if (!mUnitData->mUnknown11C)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

// FUNCTION: 0x0047bf60
void CDiveBomber::DeclareInWater()
{
	SUPERTYPE::DeclareInWater();
	if ((!mUnitData->mUnknown11C) && (!mUnitData->mUnknown124))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

//******************************************************************************************
// The hit answer is CSmallAirUnit's (AirUnit.cpp): the same body, so the linker folds it (/OPT:ICF) and retail's slot 39
// points at 00403ba0. It names CAirUnit's Hit, as CSmallAirUnit's SUPERTYPE call does.
// FUNCTION: 0x00403ba0
void CDiveBomber::Hit(CThing *other_thing, CCollisionReport *report)
{
	// a dying aircraft explodes on what it flies into
	if ((!mUnitData->mUnknown11C) && (mFlags & TF_DYING) &&
		((other_thing->IsA(THING_TYPE_UNIT)) || (other_thing->IsA(THING_TYPE_TREE | THING_TYPE_CAN_BE_WALKED_ON))))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}

	CAirUnit::Hit(other_thing, report);
}
