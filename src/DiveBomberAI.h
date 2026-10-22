// DiveBomberAI.h: CDiveBomberAI (0x74 bytes, vtable 005db1ac), a dive bomber's AI (DiveBomberAI.obj). Its
// constructor is inline (CDiveBomber::Init 0044529e).

#ifndef DIVEBOMBERAI_H
#define DIVEBOMBERAI_H

#include "UnitAI.h"

class CDiveBomberAI : public CUnitAI
{
public:
	CDiveBomberAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mMode = 0; mUnknown68 = 0; mUnknown64 = 0; mUnknown6C = 0; }
	virtual float	Unknown3();						// 3 (00446480)
	virtual void	Unknown8(CEvent *event);		// 8 (004457d0)
	virtual void	Unknown9(CEvent *event);		// 9 (00445900)
			float	Attack();						// 00445ad0: mode 0, a dive at the target
			float	Unknown445f40();				// 00445f40: mode 1
			float	Unknown446150();				// 00446150: mode 2
			void	Unknown446400();				// 00446400
			void	Retreat();						// inline (DiveBomberAI.cpp): the end of a dive
			float	TargetRadius();					// inline (DiveBomberAI.cpp): the target's radius

	SINT			mMode;							// +0x60: 0 dive, 1, 2 (slot 9 picks it from the weapon and target)
	BOOL			mUnknown64;						// +0x64: diving
	BOOL			mUnknown68;						// +0x68: bay open
	SINT			mUnknown6C;						// +0x6c
	float			mUnknown70;						// +0x70
};

#endif
