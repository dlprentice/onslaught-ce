// WarspiteAI.h: CWarspiteAI (vtable 005dfbdc), the Warspite's AI, with one slot more than CUnitAI (WarspiteAI.obj).
// Kept out of UnitAI.h: one more class there flips CGame::DrawDebugStuff (00470650) through the declaration-count
// tie-break.

#ifndef WARSPITEAI_H
#define WARSPITEAI_H

#include "UnitAI.h"

class CWarspiteAI : public CUnitAI
{
public:
	CWarspiteAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown12();					// 12 (005046a0)
};

#endif
