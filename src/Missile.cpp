// Missile.cpp: reconstructed from Missile.obj (004ba9f0..004bac35); Stuart's release has no copy. A missile is a
// round with its own mesh (the round definition's +0x0c, SetMesh's).

#include "common.h"
#include "missile.h"
#include "WorldPhysicsManager.h"
#include "InitThing.h"
#include "RenderThing.h"

// FUNCTION: 0x004ba9f0 init:ZERO_FVECTOR
// FUNCTION: 0x004baa10 init:ID_FMATRIX

//******************************************************************************************
void CMissile::Init(CInitThing *init)	// FUNCTION: 0x004baae0
{
	char	*mesh = mData->mUnknown0c;
	if (mesh)
	{
#line 11
		CRenderData	*data = new(MEMTYPE_SCRATCHPAD) CRenderData;
		CInitRenderThing	*rinit = &data->mInit[0];
		data->mInit[0].mThing = this;	// STAND-IN (order; review F 2026-10-15): this store through data, the rest through rinit; `rinit->mThing` loses 004baae0
		data->mRTID[0] = 1;
		strcpy(rinit->mName, mData->mUnknown0c);

		mRenderThing = CreateRenderThing(data->mRTID[0]);
		if (mRenderThing)
			mRenderThing->Init(rinit);
		delete data;
	}
	SUPERTYPE::Init(init);
}

//******************************************************************************************
// CRound's slot 71 is out of line here (004bac2b, folded into the shared empty 00452da0).
void CMissile::AccumulateResources(CResourceAccumulator *accumulator, DWORD flags)	// FUNCTION: 0x004bac10
{
	mRenderThing->AccumulateResources(accumulator, flags);
	SUPERTYPE::AccumulateResources(accumulator, flags);
}
