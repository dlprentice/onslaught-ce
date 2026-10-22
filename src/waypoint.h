// waypoint.h: CWaypoint, a point on a waypoint path (0x40 bytes; vtables 005dd2f0 and 005dd278; Waypoint.obj,
// 005056c0..00505826). Not in Stuart's release. Its inline members were emitted by the thing factory's object
// (the name 004bfb50, SetThingType 004bfb60, the destructor 004bfe70), its class id by a shared return-18 (0052db60).
// Every waypoint joins one global set (00855120) that the old path format indexes (CWaypointPath::Load).

#ifndef WAYPOINT_H
#define WAYPOINT_H

#include "thing.h"
#include "SPtrSet.h"

#define THING_TYPE_WAYPOINT		0x00001000		// CWaypoint::SetThingType (004bfb64); CWaypointPath::Load tests it (00505a2c)

class CWaypoint : public CThing
{
public:
	typedef CThing SUPERTYPE;
	virtual char	*_GetClassName() { return "CWaypoint"; }
	virtual int		_GetClassID() { return 18; }							// 0052db60

	virtual void	Init(CInitThing *init);									// 005057b0
	virtual void	Shutdown();												// 00505810
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_WAYPOINT); }

	CActiveReader<CThing>	mNext;										// +0x3c: the init thing's target, cast (005057f9)
};

#endif
