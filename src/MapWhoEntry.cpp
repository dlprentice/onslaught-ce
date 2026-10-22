// MapWhoEntry.cpp: entries embedded in CThing, reconstructed from the pristine retail object.
#include "common.h"
#include "mapwhoentry.h"
#include "mapwho.h"
#include "thing.h"
#include "mesh.h"

// FUNCTION: 0x00492ab0 init:ZERO_FVECTOR
// FUNCTION: 0x00492ad0 init:ID_FMATRIX

void CMapWhoEntry::Init(const FVector &pos, CThing *thing, float force_radius)	// FUNCTION: 0x00492ba0
{
	mNext = NULL;
	mPrev = NULL;
	float radius = thing->GetRadius();
	CBoundingBox *box = thing->GetBoundingBox();
	if (box)
	{
		float x = fabsf(box->mOrigin.X) + box->mAxes.X;
		float y = fabsf(box->mOrigin.Y) + box->mAxes.Y;
		radius = sqrt(x * x + y * y);
	}
	if (force_radius >= 0)
		radius = force_radius;
	mSectorIn = MAP_WHO.WorldToSector(pos, MAP_WHO.GetLevelForRadius(radius));
	MAP_WHO.AddEntry(this);
}

CMapWhoEntry::CMapWhoEntry()	// FUNCTION: 0x00492c60
{
	mSectorIn.mLayer = -1;
}

void CMapWhoEntry::RemoveFromMap()	// FUNCTION: 0x00492c70
{
	if (mSectorIn.mLayer != -1)
		MAP_WHO.RemoveEntry(this);
}

// The destructor expands RemoveFromMap; the body is the same, so the linker keeps one copy (00492c70) and the
// destructors' unwind code jumps there.
CMapWhoEntry::~CMapWhoEntry()
{
	RemoveFromMap();
}

CThing *CMapWhoEntry::GetThing()	// FUNCTION: 0x00492c90
{
	return (CThing *)((char *)this - 0x0c);
}

BOOL CMapWhoEntry::UpdateEntry(const FVector &pos)	// FUNCTION: 0x00492ca0
{
	CSector sector = MAP_WHO.WorldToSector(pos, mSectorIn.mLayer);
	if (!(mSectorIn == sector))
	{
		RemoveFromMap();
		mSectorIn = sector;
		MAP_WHO.AddEntry(this);
		return TRUE;
	}
	else
		return FALSE;
}

// The second pair of maths constants, after the code (see mathsconst2.h).
#include "mathsconst2.h"	// FUNCTION: 0x00492d30 init:ZERO_FVECTOR2
						// FUNCTION: 0x00492d50 init:ID_FMATRIX2
