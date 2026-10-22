// SafeSide.cpp: reconstructed from SafeSide.obj (004de0a0..004de1e6); Stuart's release has no copy.

#include "common.h"
#include "SafeSide.h"
#include "InitThing.h"
#include "world.h"

// FUNCTION: 0x004de0a0 init:ZERO_FVECTOR
// FUNCTION: 0x004de0c0 init:ID_FMATRIX

void CSafeSide::Init(CInitThing *init)	// FUNCTION: 0x004de190
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	WORLD.mSetD0.Add(this);
	mAllegiance = init->mAllegiance;
	SUPERTYPE::Init(init);
}

void CSafeSide::Shutdown()	// FUNCTION: 0x004de1d0
{
	WORLD.mSetD0.Remove(this);
	SUPERTYPE::Shutdown();
}
