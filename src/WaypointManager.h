// WaypointManager.h: the waypoint paths (WaypointManager.obj, 00505830..00505d0b; not in Stuart's release). A path
// (vtable 005dfc8c, 0x18 bytes) is a name and the waypoints on it; the manager keeps every path in one class-static
// set (00854fc0; its atexit thunk carries the guard bit, 00505940).

#ifndef WAYPOINTMANAGER_H
#define WAYPOINTMANAGER_H

#include "SPtrSet.h"
#include "waypoint.h"

class CWaypoint;
class CThing;
class CDXMemBuffer;

class CWaypointPath
{
public:
	CWaypointPath() { mName = NULL; }
	virtual			~CWaypointPath();											// 00505bd0
	void			Load(CDXMemBuffer *file, SINT version, CThing **things);	// 00505960

	char			*mName;						// +0x04
	SPtrSet<CWaypoint>	mWaypoints;				// +0x08
};

class CWaypointManager
{
public:
	static void			DeleteAll();											// 00505ab0
	static void			Load(CDXMemBuffer *file, SINT version, CThing **things);	// 00505ae0
	static CWaypoint	*FindNearest(char *path, FVector *pos);					// 00505c30

	static SPtrSet<CWaypointPath>	mPaths;										// 00854fc0
};

#endif
