// hazard.h: CHazard (vtable 005e477c, 66 slots; 0x88 bytes), only what matched files need. Its inline members
// are emitted by WorldPhysicsManager.obj, whose SpawnHazard constructs one (00510179); Init is 0047e530 and
// Shutdown 0047e6e0.

#ifndef HAZARD_H
#define HAZARD_H

#include "thing.h"
#include "ParticleManager.h"

DECLARE_THING_CLASS(CHazard, CComplexThing)
public:
	virtual void			Init(CInitThing *init);									// 0047e530
	virtual	void			Shutdown();												// 0047e6e0
	virtual float			GetRadius();											// 0047e770
	virtual void			Hit(CThing *other_thing, CCollisionReport *report);	// 0047e710
	virtual	void			SetThingType(ULONG t)		 { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_20); }

	class CHazardData		*mData;				// +0x7c: the init's definition (set outside the constructor)
	CParticleEffectLink		mEffect;			// +0x80
};

#endif
