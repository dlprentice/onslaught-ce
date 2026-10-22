// radar.cpp: CRadar, a radar dish that turns on its building (reconstructed; Stuart's file is not in his release).
// oids.cpp keeps its own declaration for the factory; this is the full view.

#include	"common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#define RADAR_CPP
#include	"unit.h"
#include	"InitThing.h"

// The per-file maths constants from maths.h:
// FUNCTION: 0x004d6270 init:ZERO_FVECTOR
// FUNCTION: 0x004d6290 init:ID_FMATRIX

// A radar dish (0x284 bytes; vtable 005dd788).
DECLARE_THING_CLASS(CRadar, CUnit)
public:
	virtual void	Init(CInitThing *init);								// 004d6360
	virtual float	GetMaxVelocity() { return 0.0f; }
	virtual float	GetRadius() { return 0.6f; }
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_STATIONARY); }
	virtual float	Gravity() { return 0; }
	virtual void	Move();												// 004d63c0

	FMatrix			mDish;												// +0x250: the dish's turn
	float			mAngle;												// +0x280
};

//*********************************************************************************
void	CRadar::Init(CInitThing *init)	// FUNCTION: 0x004d6360
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;
	SUPERTYPE::Init(init);

	mDish = ID_FMATRIX;
	mAngle = (rand() % 6283) / 1000.0f - PI;
}

//*********************************************************************************
void	CRadar::Move()	// FUNCTION: 0x004d63c0
{
	SUPERTYPE::Move();

	mAngle += 0.1f;
	if (mAngle > PI)
		mAngle -= PI * 2;
	mDish.MakeRotationYawF(mAngle);
}
