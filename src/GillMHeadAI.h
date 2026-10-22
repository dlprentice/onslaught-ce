// GillMHeadAI.h: CGillMHeadAI (0x64 bytes, vtable 005dbcec), the AI of the GillM's head (GillMHeadAI.obj): it
// sweeps its aim from side to side within PI/4 of the body's heading and fires where it faces. The constructor is
// inline (CGillMHead 0047a760).

#ifndef GILLMHEADAI_H
#define GILLMHEADAI_H

#include "UnitAI.h"

class CGillMHeadAI : public CUnitAI
{
public:
	CGillMHeadAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mSweepRight = FALSE; }
	virtual float	Unknown3();						// 3 (0047afc0)
	virtual void	Unknown4();						// 4 (0047b090)
	virtual void	Unknown9(CEvent *event);		// 9 (0047aad0)
	virtual CUnit	*Unknown11();					// 11 (0047b100)

	BOOL			mSweepRight;					// +0x60: the sweep's direction
};

#endif
