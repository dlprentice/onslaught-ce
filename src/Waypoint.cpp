// Waypoint.cpp: reconstructed from Waypoint.obj (005056c0..00505826); Stuart's release has no copy.
#include "common.h"
#include "waypoint.h"			// FUNCTION: 0x005056c0 init:ZERO_FVECTOR
#include "world.h"
#include "map.h"				// FUNCTION: 0x005056e0 init:ID_FMATRIX

//******************************************************************************************
void CWaypoint::Init(CInitThing *init)	// FUNCTION: 0x005057b0
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	SUPERTYPE::Init(init);
	WORLD.mWaypoints.Append(this);
	if (MAP.Collide(mPos) < mPos.Z)
		mPos.Z = MAP.Collide(mPos);
	mNext.SetReader((CThing *)init->mTarget);
}

//******************************************************************************************
void CWaypoint::Shutdown()	// FUNCTION: 0x00505810
{
	WORLD.mWaypoints.Remove(this);
	SUPERTYPE::Shutdown();
}
