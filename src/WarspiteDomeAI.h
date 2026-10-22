// WarspiteDomeAI.h: CWarspiteDomeAI (0x60 bytes, vtable 005dfc14), the Warspite dome's AI (WarspiteDomeAI.obj). The
// constructor is inline (CWarspiteDome 00504880).

#ifndef WARSPITEDOMEAI_H
#define WARSPITEDOMEAI_H

#include "UnitAI.h"

class CWarspiteDomeAI : public CUnitAI
{
public:
	CWarspiteDomeAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual CUnit	*Unknown11();					// 11 (00504e50)
};

#endif
