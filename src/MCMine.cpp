// MCMine.cpp: reconstructed from MCMine.obj (0049c2f0..0049c4df); Stuart's release has no copy. A mine's motion
// controller: the mine sinks, and its mesh with it.

#include "common.h"
#include "Motioncontroller.h"
#include "mesh.h"
#include "unit.h"

// FUNCTION: 0x0049c2f0 init:ZERO_FVECTOR
// FUNCTION: 0x0049c310 init:ID_FMATRIX

//******************************************************************************************
CMCMine::CMCMine(IRenderableThing *thing)	// FUNCTION: 0x0049c3e0
{
	mThing = thing;
}

// FUNCTION: 0x0049c400 ??_GCMCMine@@UAEPAXI@Z

//******************************************************************************************
CMCMine::~CMCMine()	// FUNCTION: 0x0049c420
{
	mThing = NULL;
	mLastDepth = -99999.9f;
}

//******************************************************************************************
void CMCMine::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x0049c440
{
	CMine	*mine = (CMine *)mThing;
	if (!part->mPartNumber)
	{
		float	frac = mine->GetFractionTime();
		float	old = mine->mOldDepth;
		pos->Z -= (mine->mDepth - old) * frac + old;
	}
	mLastDepth = mine->mDepth;
}

//******************************************************************************************
BOOL CMCMine::Unknown8()	// FUNCTION: 0x0049c4b0
{
	CMine	*mine = (CMine *)mThing;
	return mLastDepth != mine->mDepth;
}
