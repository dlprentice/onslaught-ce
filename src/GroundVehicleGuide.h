// GroundVehicleGuide.h: CGroundVehicleGuide (0x44 bytes, vtable 005dbd90), the guide CGroundVehicle::Init makes
// (0047d158). It follows a path of occupancy-grid cells to its destination. Its object is GroundVehicleGuide.obj
// (0047d4a0..0047e18d).

#ifndef GROUNDVEHICLEGUIDE_H
#define GROUNDVEHICLEGUIDE_H

#include "Guide.h"
#include "Array.h"
#include "squad.h"

class COccupancyGrid;

class CGroundVehicleGuide : public CGuide
{
public:
	CGroundVehicleGuide(CUnit *unit);								// 0047d590
	virtual void	Unknown3();										// 3 (0047d750)
	virtual CNormalSquadUnknownD4	*GetPath();						// 9 (folded with the infantry and mech guides', 0048a4b0)

	COccupancyGrid			*mGrid;									// +0x20: the unit's grid (CUnit 004fd380)
	CNormalSquadUnknownD4	mPath;									// +0x24
};

#endif
