// Carrier.cpp: reconstructed from Carrier.obj (00421990..00421cbe); Stuart's release has no copy. The carrier, a big
// aircraft with its own guide and AI. The allocations' __LINE__s are the retail ones; the #line places them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "CarrierAI.h"
#include "CarrierGuide.h"

// FUNCTION: 0x00421990 init:ZERO_FVECTOR
// FUNCTION: 0x004219b0 init:ID_FMATRIX

//******************************************************************************************
void CCarrier::Init(CInitThing *init)	// FUNCTION: 0x00421a80
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_BUILDING | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_TREE | THING_TYPE_UNKNOWN_08000000;

	SUPERTYPE::Init(init);

#line 26
	mGuide = new(MEMTYPE_GUIDE) CCarrierGuide(this);
	mAI = new(MEMTYPE_AI) CCarrierAI(this, init);
}

// The AI's implicit destructors, emitted here with its vtable.
// FUNCTION: 0x00421b80 ??_GCCarrierAI@@UAEPAXI@Z
// FUNCTION: 0x00421ba0 ??1CCarrierAI@@UAE@XZ

//******************************************************************************************
void CCarrier::Move()	// FUNCTION: 0x00421c40
{
	if (mFlags & TF_DYING)
	{
		mUnknown120.mPitch = 0.2f;
		mUnknown14C = FVector(0, 0, 0);
	}
	mCurrentOrientation.mRoll *= 0.999f;
	SUPERTYPE::Move();
}

//******************************************************************************************
void CCarrier::Stop()	// FUNCTION: 0x00421cb0
{
	if (mUnknown168 != 1)
		SUPERTYPE::Stop();
}
