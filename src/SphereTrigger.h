// SphereTrigger.h: the 0x9c-byte thing built by SpawnThing. Retail Hit (004e5700) stores deletion readers in
// the list at +0x8c, and compares the float at +0x88 with EVENT_MANAGER's time. RTTI identifies both vtables.

#ifndef SPHERETRIGGER_H
#define SPHERETRIGGER_H

#include "thing.h"
#include "ParticleManager.h"

DECLARE_THING_CLASS(CSphereTrigger, CComplexThing)
public:
	virtual void	Shutdown();								// 004e5690
	virtual void	Init(CInitThing *init);					// 004e5500
	virtual float	GetRadius() { return mRadius; }
	virtual void	Activate();								// 004e5540
	virtual void	Deactivate();							// 004e5680
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);	// 004e5700
	virtual void	*GetContainedInside() { return &mContained; }
	virtual void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | 0x08000000 | THING_TYPE_STATIONARY); }

	CParticleEffectLink				mEffect;				// +0x7c
	float							mRadius;				// +0x84
	float							mContainedTime;			// +0x88
	SPtrSet<CActiveReader<CUnit> >	mContained;				// +0x8c
};

#endif
