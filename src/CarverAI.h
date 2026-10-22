// CarverAI.h: CCarverAI (0x64 bytes, vtable 005d9448), a carver's AI (CarverAI.obj): it opens its wings to attack
// from beyond 40, closes them and turns away inside 15, and notes rounds aimed at it. The constructor is inline
// (CCarver::Init 004224e1).

#ifndef CARVERAI_H
#define CARVERAI_H

#include "UnitAI.h"

class CCarverAI : public CUnitAI
{
public:
	CCarverAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mAttacking = FALSE; mUnknown14 = 0; }
	virtual void	Unknown8(CEvent *event);		// 8 (00422aa0)
	virtual void	Unknown9(CEvent *event);		// 9 (00422b90)
			void	CheckRounds();					// 00422db0

	BOOL			mAttacking;						// +0x60
};

#endif
