// CollisionSeekingInfantryBloke.cpp: reconstructed from CollisionSeekingInfantryBloke.obj (00425920..00425a57);
// Stuart's release has no copy. An infantryman's collision seeker: it passes through its own side's units. (Infantry.cpp
// declares the class for its own use; declaring it in collisionseekingthing.h moves CGame::DrawDebugStuff.)

#include "common.h"
#include "unit.h"
#include "collisionseekingthing.h"
#include "ThingType.h"
#include "DebugLog.h"
#include "CollisionSeekingInfantryBloke.h"

// FUNCTION: 0x00425920 init:ZERO_FVECTOR
// FUNCTION: 0x00425940 init:ID_FMATRIX

//******************************************************************************************
// A friendly vehicle (not a battle engine) never collides with it. The flag test compares a mask with 1, so it never
// holds and every pair falls through to the base test (a retail bug, kept).
// STAND-IN (registers): this seeker's own thing is read into a local before the tests; that is what makes retail keep the
// mask in ebx and reuse it as the 1 it compares and returns (00425a40, 00425a45). Called through the cast instead, the
// function is 64 bytes off.
BOOL CCollisionSeekingInfantryBloke::Unknown8(CCollisionSeekingThing *other)	// FUNCTION: 0x00425a10
{
	CThing	*thing = other->mForThing;
	CUnit	*unit = (CUnit *)mForThing;	// STAND-IN (registers), see above
	if ((thing->IsA(THING_TYPE_VEHICLE) == TRUE) && (!thing->IsA(THING_TYPE_BATTLE_ENGINE)) &&
		(unit->IsTargetAlligence(((CUnit *)thing)->mAllegiance) == TRUE))
		return TRUE;
	return CCollisionSeekingThing::Unknown8(other);
}
