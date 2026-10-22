// BomberGuide.h: CBomberGuide (0x30 bytes, vtable 005d8dbc), a bomber's guide: an air guide (BomberGuide.obj). The
// constructor is inline (CBomber::Init 0041610c); its implicit destructor is emitted in Bomber.obj (00416280).

#ifndef BOMBERGUIDE_H
#define BOMBERGUIDE_H

#include "AirGuide.h"

class CBomberGuide : public CAirGuide
{
public:
	CBomberGuide(CUnit *unit) : CAirGuide(unit) {}
	virtual void	Unknown3();						// 3 (00416970)
};

#endif
