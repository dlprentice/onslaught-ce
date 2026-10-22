// explosion.h: CExplosion (vtable 005e4454, 68 slots; 0x94 bytes), only what matched files need. Its inline
// members are emitted by WorldPhysicsManager.obj, whose SpawnExplosion constructs one (0050ff39).

#ifndef EXPLOSION_H
#define EXPLOSION_H

#include "thing.h"
#include "activereader.h"

DECLARE_THING_CLASS(CExplosion, CComplexThing)
public:
	virtual void			HandleEvent(CEvent *event);								// 0044c140
	virtual void			Init(CInitThing *init);									// 0044b930
	virtual float			GetRadius();											// 0044c160
	float					GetCurrentRadius()	{ return mCurrentRadius; }		// inline (CUnit::Damage 004f9c7d)
	virtual	void			SetThingType(ULONG t);									// 0044c180
	virtual void			Hit(CThing *other_thing, CCollisionReport *report);	// 0044bf10
	virtual BOOL			CanGoUnderWater()		{ return TRUE; }				// 49 (the shared 004014a0)
	virtual void			Unknown66();											// 0044c0f0
	virtual float			Unknown67();											// 0044c170: the damage

	float					mCurrentRadius;		// +0x7c: grows to mRadius (Init 0044ba4b; CUnit::Damage reads it, 004f9c7d)
	float					mRadius;			// +0x80 (GetRadius 0044c160)
#ifdef EXPLOSION_CPP	// explosion.cpp's view; the other objects keep theirs (declaration counts)
	class CExplosionData	*mBehaviour;		// +0x84: the physics script's (the init thing's, 0044b999)
	ECollisionType			mColType;			// +0x88: what it went off against; picks the effect and sound
	SINT					mAllegiance;		// +0x8c: a smart explosion spares its own side (Hit 0044bf44)
#else
	UBYTE					mUnknown84[0x90 - 0x84];
#endif
	CActiveReader<CThing>	mOriginator;			// +0x90: the originator, credited with kills (the destructor 0050fff0 unregisters it)
};

#endif
