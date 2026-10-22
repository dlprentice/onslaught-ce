// BomberAI.h: CBomberAI (0x64 bytes, vtable 005d8d88), a bomber's AI (BomberAI.obj): it makes bombing runs over its
// target, breaking off to come round again once it has passed over. The constructor is inline (CBomber::Init).

#ifndef BOMBERAI_H
#define BOMBERAI_H

#include "UnitAI.h"

class CBomberAI : public CUnitAI
{
public:
	CBomberAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mRun = FALSE; }
	virtual float	Unknown3();						// 3 (00416790)
	virtual void	Unknown8(CEvent *event);		// 8 (004164b0, folded with the plane's and submarine's)
	virtual void	Unknown9(CEvent *event);		// 9 (00416590)

	BOOL			mRun;							// +0x60: on a bombing run
};

#endif
