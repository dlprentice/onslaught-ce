// WaypointManager.cpp: reconstructed from WaypointManager.obj (00505830..00505d0b); Stuart's release has no copy.
// The allocations' __LINE__ values are the retail ones.
#include "common.h"
#include "WaypointManager.h"	// FUNCTION: 0x00505830 init:ZERO_FVECTOR
#include "waypoint.h"			// FUNCTION: 0x00505850 init:ID_FMATRIX
#include "world.h"
#include "DXMemBuffer.h"
#include "DebugLog.h"
#include "console.h"

SPtrSet<CWaypointPath>	CWaypointManager::mPaths;
// FUNCTION: 0x00505920 init:mPaths
// FUNCTION: 0x00505940 exit:mPaths

//******************************************************************************************
void CWaypointPath::Load(CDXMemBuffer *file, SINT version, CThing **things)	// FUNCTION: 0x00505960
{
	char len;
	file->Read(&len, 1);
#line 26
	mName = new(MEMTYPE_NAVIGATION) char[len + 1];
	int i;
	for (i = 0; i < len; i++)
		file->Read(&mName[i], 1);
	mName[i] = 0;

	if (version >= 33)
	{
		int count;
		file->Read(&count, 4);
		while (count)
		{
			short index;
			file->Read(&index, 2);
			LOG.AddMessage("%d %d", count, index);
			if (index >= 0 && things[index] && (things[index]->GetThingType() & THING_TYPE_WAYPOINT))
				mWaypoints.Add((CWaypoint *)things[index]);
			count--;
		}
	}
	else
	{
		short index;
		file->Read(&index, 2);
		int n = 0;
		for (CWaypoint *w = (CWaypoint *)WORLD.mWaypoints.First(); w; w = (CWaypoint *)WORLD.mWaypoints.Next())
		{
			if (n == index)
				mWaypoints.Add(w);
			n++;
		}
	}
}

//******************************************************************************************
void CWaypointManager::DeleteAll()	// FUNCTION: 0x00505ab0
{
	CWaypointPath *path;
	while (path = mPaths.First())
	{
		mPaths.Remove(path);
		delete path;
	}
}

//******************************************************************************************
void CWaypointManager::Load(CDXMemBuffer *file, SINT version, CThing **things)	// FUNCTION: 0x00505ae0
{
	short count;
	file->Read(&count, 2);
	while (count)
	{
#line 114
		CWaypointPath *path = new(MEMTYPE_NAVIGATION) CWaypointPath;
		path->Load(file, version, things);
		mPaths.Append(path);
		count--;
	}
	CONSOLE.StatusDone("Loading waypoints");
}
// FUNCTION: 0x00505bb0 ??_GCWaypointPath@@UAEPAXI@Z

//******************************************************************************************
CWaypointPath::~CWaypointPath()	// FUNCTION: 0x00505bd0
{
	if (mName)
		delete [] mName;
}

//******************************************************************************************
CWaypoint *CWaypointManager::FindNearest(char *name, FVector *pos)	// FUNCTION: 0x00505c30
{
	for (CWaypointPath *path = mPaths.First(); path; path = mPaths.Next())
	{
		char *pathName = path->mName;	// STAND-IN (order; review F 2026-10-15): the name through a local; comparing path->mName loses 00505c30
		if (_stricmp(pathName, name) == 0)
		{
			float best = 9999999.0f;
			CWaypoint *nearest = NULL;
			for (CWaypoint *w = path->mWaypoints.First(); w; w = path->mWaypoints.Next())
			{
				FVector d = w->GetPos() - *pos;
				float dist = d.Y * d.Y + d.Z * d.Z + d.X * d.X;
				if (dist < best)
				{
					best = dist;
					nearest = w;
				}
			}
			return nearest;
		}
	}
	return NULL;
}
// FUNCTION: 0x00505d00 ??0?$SPtrSet@VCWaypointPath@@@@QAE@XZ
