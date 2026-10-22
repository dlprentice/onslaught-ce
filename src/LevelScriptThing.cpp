// LevelScriptThing.cpp: reconstructed from LevelScriptThing.obj (0048ffe0..004900e3); Stuart's release has no copy.

#include "common.h"
#include "LevelScriptThing.h"
#include "InitThing.h"

// FUNCTION: 0x0048ffe0 init:ZERO_FVECTOR
// FUNCTION: 0x00490000 init:ID_FMATRIX

void CLevelScriptThing::Init(CInitThing *init)	// FUNCTION: 0x004900d0
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	SUPERTYPE::Init(init);
}
