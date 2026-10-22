// CarrierAI.h: CCarrierAI (0x60 bytes, vtable 005d93d4), a carrier's AI: CUnitAI with a slot 12 that flies it
// along its waypoints (CarrierAI.obj). Its constructor is inline; CCarrier::Init builds it (00421b30).

#ifndef CARRIERAI_H
#define CARRIERAI_H

#include "UnitAI.h"

class CCarrierAI : public CUnitAI
{
public:
	CCarrierAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown12();					// 12 (00421db0)
};

#endif
