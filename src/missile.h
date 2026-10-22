// missile.h: CMissile (vtable 005e3ba4, 72 slots), a CRound with nothing added (both are 0x134 bytes). Its inline
// members are emitted by WorldPhysicsManager.obj, whose SpawnRound constructs one (0050f807); Init is 004baae0.

#ifndef MISSILE_H
#define MISSILE_H

#include "round.h"

DECLARE_THING_CLASS(CMissile, CRound)
public:
							CMissile(CRoundData *data) : CRound(data) {}

	virtual void			Init(CInitThing *init);							// 004baae0
	// Retail slot 38 is 00401540 (type | 0x80000003), bypassing CRound's 004d8320 (type | 0x80000007).
	// FUNCTION: 0x00401540 ?SetThingType@CMissile@@UAEXK@Z
	virtual void			SetThingType(ULONG t) { CActor::SetThingType(t); }
	virtual void			AccumulateResources(CResourceAccumulator *accumulator, DWORD flags = 0);	// 71 (004bac10)
	virtual bool			GetCanBeImpostered()		{ return false; }	// IRenderableThing 18 (the shared 00453a80; CRound keeps CThing's 00401480)
};

#endif
