// CarverGuide.h: CCarverGuide (0x30 bytes, vtable 005d947c), a carver's guide (CarverGuide.obj): an air guide that
// looks further for what is near (45, and further still for rounds) and rolls over when it has been shot at.

#ifndef CARVERGUIDE_H
#define CARVERGUIDE_H

#include "AirGuide.h"

class CCarverGuide : public CAirGuide
{
public:
	CCarverGuide(CUnit *unit);						// 00422f90
	virtual void	HandleEvent(CEvent *event);		// 0 (00423490)
	virtual void	Unknown3();						// 3 (00423030)
			void	FindNearest();					// 00423510
};

#endif
