// RenderThing.cpp: reconstructed from RenderThing.obj (004d6a30..004d6c12); Stuart's release has no copy. The base
// render thing's out-of-line members; its inline ones are emitted by the objects whose vtables use them.

#include "common.h"
#include "RenderThing.h"
#include "mathsconst.h"
#include "DebugLog.h"

//******************************************************************************************
void CRenderThing::Init(CInitRenderThing *init)	// FUNCTION: 0x004d6a30
{
	mThing = init->mThing;
	mCueFactor = 0.00001f;
	mPose = NULL;
}

//******************************************************************************************
// No camera of its own: the identity at the origin, 90 degrees wide.
void CRenderThing::GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing)	// FUNCTION: 0x004d6a50
{
	ori->Row[0] = FVector(1, 0, 0);
	ori->Row[1] = FVector(0, 1, 0);
	ori->Row[2] = FVector(0, 0, 1);
	pos->X = 0;
	pos->Y = 0;
	pos->Z = 0;
	*fov = 90.0f;
}

//******************************************************************************************
SINT CRenderThing::GetMovieCameraNumber(IRenderableThing *thing)	// FUNCTION: 0x004d6b20
{
	return 0;
}

//******************************************************************************************
// The render thing's defaults that no code above defined. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address
// each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x0040c640
void CRenderThing::RenderImposter()
{
}

// FUNCTION: 0x004d6b30 init:ZERO_FVECTOR
// FUNCTION: 0x004d6b50 init:ID_FMATRIX
