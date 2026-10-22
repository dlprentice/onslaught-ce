// GroundAttackAI.h: CGroundAttackAI (0x68 bytes, vtable 005dbd4c), a ground-attack aircraft's AI
// (GroundAttackAI.obj): it opens its bay and dives on its target from out of range, pulls away once close, and
// comes round again. The constructor is inline (CGroundAttackAircraft::Init 0047bc80).

#ifndef GROUNDATTACKAI_H
#define GROUNDATTACKAI_H

#include "UnitAI.h"

class CGroundAttackAI : public CUnitAI
{
public:
	CGroundAttackAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { PullAway(); }
	virtual void	Unknown8(CEvent *event);		// 8 (0047b570)
	virtual void	Unknown9(CEvent *event);		// 9 (0047b6d0)
			void	PullAway();						// 0047bab0

	BOOL			mAttacking;						// +0x60
	float			mRange;							// +0x64: where it turns: 20..30 pulling away, 15..20 attacking
};

#endif
