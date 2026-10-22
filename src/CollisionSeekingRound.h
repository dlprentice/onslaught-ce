// CollisionSeekingRound.h: projectile collision seeker, 0x3c bytes, vtable 005de950.
// Init 00425b50 stores the round length at +0x38; its volume getter and collision admission replace slots 7 and 8.

#ifndef COLLISIONSEEKINGROUND_H
#define COLLISIONSEEKINGROUND_H

#include "collisionseekingthing.h"

class CCollisionSeekingRound : public CCSPersistentThing
{
public:
	virtual void Init(CInitCSThing *init);					// 00425b50
	virtual CGeneralVolume *Unknown7();					// 00425e30, returns mShape
	virtual BOOL Unknown8(CCollisionSeekingThing *other);	// 00425c60, ret 4

	float mRoundLength;									// +0x38
};

#endif
