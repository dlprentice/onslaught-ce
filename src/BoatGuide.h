// BoatGuide.h: CBoatGuide (0x20 bytes, vtable 005d8d5c), a boat's guide (BoatGuide.obj): the terrain guide's
// steering (TerrainGuide.cpp) without its deploy check.

#ifndef BOATGUIDE_H
#define BOATGUIDE_H

#include "Guide.h"

class CBoatGuide : public CGuide
{
public:
	CBoatGuide(CUnit *unit);						// 00415d70
	virtual void	Unknown3();						// 3 (00415d90)
};

#endif
