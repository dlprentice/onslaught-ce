// GroundAttackAircraft.cpp: reconstructed from GroundAttackAircraft.obj (0047bb00..0047c105); Stuart's release has
// no copy. The ground-attack aircraft: a small flier with its own motion controller that bobs as it flies and opens
// its bay to shoot. The allocations' __LINE__s are the retail ones; the #line places them.

#include "common.h"
#define GROUNDATTACK_FULL
#include "unit.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "mesh.h"
#include "game.h"
#include "MCGroundAttack.h"
#include "GroundAttackAI.h"		// before the guide: retail emits the AI's inline members (0047bd70) before the guide's (0047be30)
#include "GroundAttackGuide.h"

// FUNCTION: 0x0047bb00 init:ZERO_FVECTOR
// FUNCTION: 0x0047bb20 init:ID_FMATRIX

//******************************************************************************************
void CGroundAttackAircraft::Init(CInitThing *init)	// FUNCTION: 0x0047bbf0
{
	SUPERTYPE::Init(init);

#line 19
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCGroundAttack(this);
	mAI = new(MEMTYPE_AI) CGroundAttackAI(this, init);
	mGuide = new(MEMTYPE_GUIDE) CGroundAttackGuide(this);

	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("fly"), TRUE, TRUE);
	mBayState = 1;
	mBob = 0;
	mPhase = GAME.FloatRandom() * PI_M2;
}

// The AI's and guide's implicit destructors, emitted here with their vtables.
// FUNCTION: 0x0047bd70 ??_GCGroundAttackAI@@UAEPAXI@Z
// FUNCTION: 0x0047bd90 ??1CGroundAttackAI@@UAE@XZ
// FUNCTION: 0x0047be30 ??_GCGroundAttackGuide@@UAEPAXI@Z
// FUNCTION: 0x0047be50 ??1CGroundAttackGuide@@UAE@XZ

//******************************************************************************************
void CGroundAttackAircraft::Move()	// FUNCTION: 0x0047beb0
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
		mUnknown120.mPitch = 2.0f;
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
void CGroundAttackAircraft::DeclareInWater()	// FUNCTION: 0x0047bf60
{
	SUPERTYPE::DeclareInWater();
	if ((!mUnitData->mUnknown11C) && (!mUnitData->mUnknown124))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

//******************************************************************************************
void CGroundAttackAircraft::OpenBay()	// FUNCTION: 0x0047bfa0
{
	if ((mBayState == 1) || (mBayState == 3))
	{
		mBayState = 2;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("open"), TRUE, FALSE);
	}
}

//******************************************************************************************
void CGroundAttackAircraft::CloseBay()	// FUNCTION: 0x0047bff0
{
	if ((mBayState == 0) || (mBayState == 2))
	{
		mBayState = 3;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("close"), TRUE, FALSE);
	}
}

//******************************************************************************************
// The else-if (one shared return FALSE) is what lets retail save EBX only inside the block (0047c05b).
BOOL CGroundAttackAircraft::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x0047c040
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
// The hit answer is CSmallAirUnit's (AirUnit.cpp): the same body, so the linker folds it (/OPT:ICF) and retail's slot 39
// points at 00403ba0. It names CAirUnit's Hit, as CSmallAirUnit's SUPERTYPE call does.
// FUNCTION: 0x00403ba0
void CGroundAttackAircraft::Hit(CThing *other_thing, CCollisionReport *report)
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

//******************************************************************************************
// The ground-attack aircraft's ground answer (as CPlane's, Plane.cpp) that no code above defined. Each compiles to a
// body another function already has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at
// the survivor, whose address each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x004d20a0
void CGroundAttackAircraft::DeclareOnGround()
{
	SUPERTYPE::DeclareOnGround();
	if (!mUnitData->mUnknown11C)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}
