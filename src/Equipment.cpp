// Equipment.cpp: reconstructed from Equipment.obj (0044a740..0044a9bd); Stuart's release has no copy. A unit's piece of
// equipment sits on one of its mesh's emitters.

#include "common.h"
#include "Equipment.h"
#include "unit.h"

// FUNCTION: 0x0044a740 init:ZERO_FVECTOR
// FUNCTION: 0x0044a760 init:ID_FMATRIX

//******************************************************************************************
void CEquipment::Init(CInitEquipment &init)	// FUNCTION: 0x0044a830
{
	mAttachedTo = init.mAttachedTo;
	mNumber = init.mNumber;
	mMount = init.mMount;
}

//******************************************************************************************
// Its emitter's position, or the unit's own when it has none (or the emitter sits at the origin).
FVector CEquipment::GetPrimaryPosition()	// FUNCTION: 0x0044a850
{
	if (mNumber != -1)
	{
		FVector	pos;
		FMatrix	ori;
		mAttachedTo->GetEmitter(mMount, mNumber, pos, ori);
		if (pos == FVector(0, 0, 0))
			return mAttachedTo->GetPos();
		return pos;
	}
	return mAttachedTo->GetPos();
}

//******************************************************************************************
FMatrix CEquipment::GetPrimaryOrientation()	// FUNCTION: 0x0044a930
{
	if (mNumber != -1)
	{
		FVector	pos;
		FMatrix	ori;
		mAttachedTo->GetEmitter(mMount, mNumber, pos, ori);
		if (pos != FVector(0, 0, 0))
			return ori;
	}
	return mAttachedTo->GetOrientation();
}
