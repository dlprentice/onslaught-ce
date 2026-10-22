// Occupancy.h: COccupancy, the occupancy grids' static operations (NavigationMap.cpp defines them; CWorld calls them).

#ifndef OCCUPANCY_H
#define OCCUPANCY_H

class CUnit;

class COccupancy
{
public:
	static void		AddUnit(CUnit *unit);		// 004bc480
	static void		RemoveUnit(CUnit *unit);	// 004bc3e0
	static void		ClearSteep();				// 004bc8d0: cells under water close on every grid, too steep on each grid that cannot climb them (CWorld::LoadWorld 0050d456, worlds before version 47)
	static void		ApplyStatic();				// 004bcbf0: the static mask closes cells on every grid (CWorld::LoadWorld 0050d473, on a restart)
	static void		Rasterize(SINT x0, SINT y0, SINT x1, SINT y1, BOOL a);	// 004bd5c0
	static void		BuildStaticShadows(CUnit *unit);	// 004bd9e0
	static void		BuildStatic();				// 004bcd60: every unit's cells close on the static mask and every grid (CWorld::LoadWorld 0050d47a)
};

#endif
