// Guide.cpp: reconstructed from Guide.obj (0047e1a0..0047e436); Stuart's release has no copy. CGuide, what steers a
// unit towards the point its AI picks: it keeps the destination and how it should get there.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "weapon.h"
#include "Guide.h"

// FUNCTION: 0x0047e1a0 init:ZERO_FVECTOR
// FUNCTION: 0x0047e1c0 init:ID_FMATRIX

//******************************************************************************************
CGuide::CGuide(CUnit *unit)	// FUNCTION: 0x0047e290
{
	mUnit = unit;
	mDestination = unit->GetPos();
	mMode = 0;
}

//******************************************************************************************
void CGuide::GoToPoint(FVector point, BOOL inOverride)	// FUNCTION: 0x0047e2d0
{
	if ((!inOverride) && (mUnit->mAI->mAIMode == 2))
		return;
	mMode = 1;
	mDestination = point;
}

//******************************************************************************************
void CGuide::Unknown5(FVector v)	// FUNCTION: 0x0047e310
{
	mMode = 2;
	mDestination = v;
}

//******************************************************************************************
void CGuide::Unknown6(FVector v)	// FUNCTION: 0x0047e340
{
	mMode = 3;
	mDestination = v;
}

//******************************************************************************************
void CGuide::Unknown7(FVector v)	// FUNCTION: 0x0047e370
{
	if (mUnit->mAI->mAIMode == 2)
		return;
	mMode = 3;
	if ((mUnit->mCurrentWeapon) && (mUnit->mCurrentWeapon->mUnknown94))
		mMode = 0;
	mDestination = v;
}

//******************************************************************************************
void CGuide::Stop()	// FUNCTION: 0x0047e3d0
{
	mMode = 0;
	mDestination = mUnit->GetPos();
	mUnit->mUnknown14C = FVector(0, 0, 0);
}
