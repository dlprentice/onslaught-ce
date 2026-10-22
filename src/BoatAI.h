// BoatAI.h: CBoatAI (0x60 bytes, vtable 005d8ce8), a boat's AI (BoatAI.obj): CUnitAI with its own waypoint step.
// The constructor is inline (CBoat::Init).

#ifndef BOATAI_H
#define BOATAI_H

#include "UnitAI.h"

class CBoatAI : public CUnitAI
{
public:
	CBoatAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown8(CEvent *event);		// 8 (00415b90)
};

#endif
