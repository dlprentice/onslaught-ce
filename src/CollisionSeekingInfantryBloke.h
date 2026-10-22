// CollisionSeekingInfantryBloke.h: an infantryman's collision seeker (vtable 005dbf48; CollisionSeekingInfantryBloke.cpp
// defines Unknown8, Infantry.cpp builds one at Init 00488bf3). Declaring it in collisionseekingthing.h instead moves
// CGame::DrawDebugStuff (00470650) off its declaration-count threshold, so only these two files include it.

#ifndef COLLISIONSEEKINGINFANTRYBLOKE_H
#define COLLISIONSEEKINGINFANTRYBLOKE_H

#include "collisionseekingthing.h"

class CCollisionSeekingInfantryBloke : public CCSPersistentThing
{
public:
	virtual BOOL	Unknown8(CCollisionSeekingThing *other);	// 8 (00425a10)
};

#endif
