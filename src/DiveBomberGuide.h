// DiveBomberGuide.h: CDiveBomberGuide (0x30 bytes, vtable 005db180), a dive bomber's guide (DiveBomberGuide.obj):
// an air guide with its own flight. Its constructor is inline (CDiveBomber::Init 004452f2).

#ifndef DIVEBOMBERGUIDE_H
#define DIVEBOMBERGUIDE_H

#include "AirGuide.h"

class CDiveBomberGuide : public CAirGuide
{
public:
	CDiveBomberGuide(CUnit *unit) : CAirGuide(unit) {}
	virtual void	Unknown3();						// 3 (00446840)
};

#endif
