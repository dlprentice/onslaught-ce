// GroundVehicle.cpp: reconstructed from GroundVehicle.obj (0047cee0..0047d49f); Stuart's release has no copy. A
// ground vehicle: wheeled ones drive their wheels through a buggy motion controller; the rest tilt with the ground.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "mcbuggy.h"
#include "Motioncontroller.h"
#include "GroundVehicleGuide.h"
#include "UnitAI.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "map.h"
#include "WorldPhysicsManager.h"

// FUNCTION: 0x0047cee0 init:ZERO_FVECTOR
// FUNCTION: 0x0047cf00 init:ID_FMATRIX

//******************************************************************************************
void CGroundVehicle::Init(CInitThing *init)	// FUNCTION: 0x0047cfd0
{
	SUPERTYPE::Init(init);

	float	turn = ((CUnitInitThing *)init)->mStats->mTurnRate;
	mUnknown12C = CEulerAngles(turn, turn, turn);
	mMaxShields = mShields = ((CUnitInitThing *)init)->mStats->mGroundShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mGroundShieldRegeneration;
	mTiltsWithGround = 1;

	if (mRenderThing->GetRTMesh()->GetAnimModeByName("WheelMotion") != AM_INVALID)
	{
#line 28
		CMCBuggy	*mc = new(MEMTYPE_MOTIONCONTROLLER) CMCBuggy(this);
		mMotionController = mc;
		mc->SetBuggyParameters(((CUnitInitThing *)init)->mStats->mBodyMotion);
		mTiltsWithGround = 0;
	}
	else
#line 33
		mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCGroundVehicle(this);

#line 35
	mGuide = new(MEMTYPE_GUIDE) CGroundVehicleGuide(this);
#line 37
	mAI = new(MEMTYPE_AI) CUnitAI(this, init);
}

//******************************************************************************************
// It lies on the ground under it, facing its yaw, as CMine::Init: the forward vector is the matrix times
// (0, 1, 0), and the side vector is default-constructed and assigned after the pitch is stored. This keeps
// the second cross product's FVector constructor out of line. The complete tilt predicate returns the
// byte flag as a BOOL after ObeyGravity(), preserving retail's zero-extension and short-circuit order.
static BOOL GroundVehicleTiltsWithGround(CGroundVehicle &vehicle)
{
	if (vehicle.ObeyGravity())
		return vehicle.mTiltsWithGround;
	return FALSE;
}

void CGroundVehicle::Move()	// FUNCTION: 0x0047d1c0
{
	if (GroundVehicleTiltsWithGround(*this))
	{
		FMatrix	ori;
		ori.MakeRotationYawF(mCurrentOrientation.mYaw);
		FVector	forward = ori * FVector(0.0f, 1.0f, 0.0f);
		FVector	normal = MAP.Normal(mPos);
		forward = forward ^ normal;
		FVector	up = forward ^ normal;
		up.Normalise();
		mUnknown120.mPitch = -up.Z;
		FVector	side;
		side = forward;
		side.Normalise();
		mUnknown120.mRoll = side.Z;
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
BOOL CGroundVehicle::Unknown86()	// FUNCTION: 0x0047d3b0
{
	BOOL	r = SUPERTYPE::Unknown86();
	if (((!mUnitData->mUnknown108) || (mDeployState == 4)) && (mRenderThing->GetRTMesh()->GetAnimModeByName("prefire") != AM_INVALID))
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("prefire"), TRUE, FALSE);
	return r;
}

//******************************************************************************************
void CGroundVehicle::Unknown87()	// FUNCTION: 0x0047d420
{
	SUPERTYPE::Unknown87();
	if (mUnitData->mParticles[7])
	{
		if (mRenderThing->GetRTMesh()->GetAnimModeByName("postfire") != AM_INVALID)
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("postfire"), TRUE, FALSE);
	}
	else if (mRenderThing->GetRTMesh()->GetAnimModeByName("firing") != AM_INVALID)
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("firing"), TRUE, FALSE);
}

//******************************************************************************************
// The ground vehicle's death (as CCannon's, Cannon.cpp; both derive from CGroundUnit). It compiles to CCannon's body,
// so the linker folds it into that one (/OPT:ICF).

// FUNCTION: 0x0041b590
BOOL CGroundVehicle::StartDieProcess()
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	Remove();
	return TRUE;
}
