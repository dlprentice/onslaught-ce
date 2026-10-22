// GroundAttackGuide.h: CGroundAttackGuide (0x30 bytes, vtable 005dbd20), a ground-attack aircraft's guide
// (GroundAttackGuide.obj): an air guide with its own flight. Its constructor is inline
// (CGroundAttackAircraft::Init 0047bcd3).

#ifndef GROUNDATTACKGUIDE_H
#define GROUNDATTACKGUIDE_H

#include "AirGuide.h"

class CGroundAttackGuide : public CAirGuide
{
public:
	CGroundAttackGuide(CUnit *unit) : CAirGuide(unit) {}
	virtual void	Unknown3();						// 3 (0047c200)
};

#endif
