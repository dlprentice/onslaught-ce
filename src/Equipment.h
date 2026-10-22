// Equipment.h: CEquipment, what a unit carries (Stuart's name, BattleEngine.h; the object between engine.obj and
// EscapePod.obj is Equipment.cpp). Stuart's release has no Equipment.h.

#ifndef EQUIPMENT_H
#define EQUIPMENT_H
#include "maths.h"

#include "Monitor.h"

class CUnit;

// Stuart's name (CBattleEngineJetPart::ResetConfiguration). Its constructor clears the unit and sets the number to -1
// (00412683..00412687); CUnit::Init fills all three in place (004f8861..004f8870).
class CInitEquipment
{
public:
	CInitEquipment() { mAttachedTo = NULL; mNumber = -1; }

	CUnit		*mAttachedTo;				// +0x00
	SINT		mNumber;					// +0x04: the emitter index
	SINT		mMount;						// +0x08: the emitter type
};

class CEquipment : public CMonitor
{
public:
	virtual void	Init(CInitEquipment &init);			// 3 (0044a830: copies the three fields)

	FVector			GetPrimaryPosition();				// 0044a850 (Stuart's name, BattleEngine.cpp)
	FMatrix			GetPrimaryOrientation();			// 0044a930 (Stuart's name)

	CUnit			*GetAttachedTo();					// inline, defined in weapon.cpp
	CUnit			*mAttachedTo;						// +0x08
	SINT			mNumber;							// +0x0c
	SINT			mMount;								// +0x10
};

#endif
