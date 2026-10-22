// Bomber.cpp: reconstructed from Bomber.obj (00415fc0..004163bf); Stuart's release has no copy. A bomber: a big air
// unit that rolls over and dives when it dies.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "BomberAI.h"
#include "BomberGuide.h"

// FUNCTION: 0x00415fc0 init:ZERO_FVECTOR
// FUNCTION: 0x00415fe0 init:ID_FMATRIX

//******************************************************************************************
void CBomber::Init(CInitThing *init)	// FUNCTION: 0x004160b0
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	SUPERTYPE::Init(init);

#line 17
	mGuide = new(MEMTYPE_GUIDE) CBomberGuide(this);
	mAI = new(MEMTYPE_AI) CBomberAI(this, init);
}

// The AI's and the guide's implicit destructors, emitted here with their vtables.
// FUNCTION: 0x004161a0 ??_GCBomberAI@@UAEPAXI@Z
// FUNCTION: 0x004161c0 ??1CBomberAI@@UAE@XZ
// FUNCTION: 0x00416260 ??_GCBomberGuide@@UAEPAXI@Z
// FUNCTION: 0x00416280 ??1CBomberGuide@@UAE@XZ

//******************************************************************************************
// A dying bomber rolls on and noses down; it always flies the way it faces.
void CBomber::Move()	// FUNCTION: 0x004162e0
{
	if (mUnknown168 == 1)
	{
		mUnknown168 = 0;
		mUnknown1E8 = 1;
	}

	if (mFlags & TF_DYING)
	{
		mUnknown12C.mRoll = 0.2f;
		float	roll = mCurrentOrientation.mRoll + 0.8f;
		if (roll > PI)
			roll -= PI_M2;
		mUnknown120.mRoll = roll;
		mUnknown120.mPitch = PI / 2;
	}

	SUPERTYPE::Move();

	float	speed = mVelocity.Magnitude();
	FVector	v;	// declared then assigned, Stuart's occasional habit (thing.cpp:462, :482); the direct initialisation loses 004162e0
	v = FVector(speed * mOrientation.Row[0].Y, speed * mOrientation.Row[1].Y, speed * mOrientation.Row[2].Y);
	SetVelocity(v);
}

//******************************************************************************************
// The bomber's answers (as CPlane's and CGroundAttackAircraft's, Plane.cpp and GroundAttackAircraft.cpp) that no code
// above defined. Each compiles to a body another function already has, so the linker folds it into that one (/OPT:ICF):
// the retail table's slot points at the survivor, whose address each annotation names (the class header notes the
// slots' survivors too).

// FUNCTION: 0x004d20a0
void CBomber::DeclareOnGround()
{
	SUPERTYPE::DeclareOnGround();
	if (!mUnitData->mUnknown11C)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

// FUNCTION: 0x0047bf60
void CBomber::DeclareInWater()
{
	SUPERTYPE::DeclareInWater();
	if ((!mUnitData->mUnknown11C) && (!mUnitData->mUnknown124))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
