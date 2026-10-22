// PlaneAI.h: CPlaneAI (0x64 bytes, vtable 005de73c), a plane's AI (PlaneAI.obj). The constructor is inline
// (CPlane::Init 004d1a6a); the implicit destructor is emitted in Plane.obj (004d1c30).

#ifndef PLANEAI_H
#define PLANEAI_H

#include "UnitAI.h"

class CPlaneAI : public CUnitAI
{
public:
	CPlaneAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mUnknown60 = 0; mUnknown14 = 0; }
	virtual void	Unknown8(CEvent *event);		// 8 (004164b0, folded with the bomber's and submarine's)
	virtual void	Unknown9(CEvent *event);		// 9 (004d21c0)

	SINT			mUnknown60;						// +0x60
};

#endif
