// GillMAI.h: CGillMAI (0x60 bytes, vtable 005dbcb4), the GillM's AI (GillMAI.obj): it walks up to buildings and
// ships and swings at them. The constructor is inline (CGillM 00479b60).

#ifndef GILLMAI_H
#define GILLMAI_H

#include "UnitAI.h"

class CGillMAI : public CUnitAI
{
public:
	CGillMAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown9(CEvent *event);		// 9 (0047a2a0)
	virtual CUnit	*Unknown11();					// 11 (0047a400)
};

#endif
