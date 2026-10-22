// MapWho.cpp: map-grid list operations reconstructed from the pristine retail object.
#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "mapwho.h"
#include "DebugLog.h"
#include "thing.h"
#include "Geometry.h"
#include "engine.h"
#include "renderinfo.h"
// The map-who debug bodies read ID_FMATRIX at 007042d0, set by this adjacent initializer pair.
// FUNCTION: 0x00491790 init:ZERO_FVECTOR
// FUNCTION: 0x004917b0 init:ID_FMATRIX

CMapWho MAP_WHO;
// FUNCTION: 0x00491880 init:MAP_WHO
// The existing initbody resolver follows this registration to its MAP_WHO atexit body.
// FUNCTION: 0x00491890 initbody:MAP_WHO

CMapWhoNode::CMapWhoNode()	// FUNCTION: 0x00491900
{
	mChildren = NULL;
	mFirst = NULL;
}

CMapWhoNode::~CMapWhoNode()	// FUNCTION: 0x00491910
{
	if (mChildren)
		delete [] mChildren;
}

void CMapWho::Clear()	// FUNCTION: 0x00491930
{
	if (mLayers)
	{
		for (SINT layer = 4; layer >= 0; layer--)
		{
			delete [] mLayers[layer];
			mLayers[layer] = NULL;
		}
		delete [] mLayers;
		mLayers = NULL;
	}
}

// One counter, i, serves the layer-setup loop and the x loop (the shift there, the x coordinate here). With a separate x
// the y-loop's reload of x moved to the loop head: x then sits in the allocator's round-1 split batch of 16 webs with
// the ones that need an edge block on the same edge, while retail reloads it once at the latch's end.
// The shared symbol is the shared-counter lever (SwizzleTexture 0054f380); a counter-reuse sweep found it.
void CMapWho::Create()	// FUNCTION: 0x004919b0
{
// The retail allocation sites name lowercase mapwho.cpp and lines 62, 68 and 100.
#line 62 "C:\\dev\\ONSLAUGHT2\\mapwho.cpp"
	mLayers = new(MEMTYPE_MAPWHO) CMapWhoNode *[5];
	SINT width = 64;
	SINT height = 64;
	SINT i; for (i = 3; i <= 7; i++)
	{
		SINT layer = 7 - i;
#line 68
		mLayers[layer] = new(MEMTYPE_MAPWHO) CMapWhoNode[width * height];
		mShift[layer] = i;
		mWidth[layer] = width;
		mHeight[layer] = height;
		width >>= 1;
		height >>= 1;
	}
	width = 64;
	height = 64;
	for (SINT layer = 4; layer >= 1; layer--)
	{
		for (i = 0; i < width; i++)
		{
			SINT childX = i % 2;
			for (SINT y = 0; y < height; y++)
			{
				SINT childY = 2 * (y % 2);
				CSector sector((short)i, (short)y, layer);
				CMapWhoNode *node = GetSector(sector);
				CSector parentSector = sector;
				parentSector.ToParent();
				CMapWhoNode *parent = GetSector(parentSector);
				if (!parent->mChildren)
				{
#line 100
					parent->mChildren = new(MEMTYPE_MAPWHO) CMapWhoNode *[4];
					parent->mChildren[0] = NULL;
					parent->mChildren[1] = NULL;
					parent->mChildren[2] = NULL;
					parent->mChildren[3] = NULL;
				}
				// The sum twice, not a local: one `shl 2` serves both slots, where `index` takes scaled addressing twice.
				if (parent->mChildren[childX + childY])
					LOG.AddMessage("FATAL ERROR: Mapwho construction gone wrong");
				parent->mChildren[childX + childY] = node;
			}
		}
		width >>= 1;
		height >>= 1;
	}
	mQueryActive = FALSE;
}

SINT CMapWho::GetLevelForRadius(float radius)	// FUNCTION: 0x00491c50
{
	radius *= 2.01f;
	SINT layer = 4;
	while ((1 << mShift[layer]) < radius && layer >= 0)
	{
		layer--;
		if (layer < 0)
		{
			LOG.AddMessage("WARNING: Object too big for map who system");
			return 0;
		}
	}
	return layer;
}

void CMapWho::AddEntry(CMapWhoEntry *entry)	// FUNCTION: 0x00491cd0
{
	CMapWhoNode *node = GetSector(entry->mSectorIn);
	CMapWhoEntry *first = node->mFirst;
	if (first)
		first->mPrev = entry;
	entry->mNext = first;
	entry->mPrev = NULL;
	node->mFirst = entry;
}

void CMapWho::RemoveEntry(CMapWhoEntry *entry)	// FUNCTION: 0x00491d20
{
	CMapWhoEntry *prev = entry->mPrev;
	CMapWhoEntry *next = entry->mNext;
	if (prev)
		prev->mNext = next;
	if (next)
		next->mPrev = prev;
	CMapWhoNode *node = GetSector(entry->mSectorIn);
	if (entry == node->mFirst)
		node->mFirst = next;
}

CMapWhoEntry *CMapWho::GetFirstEntry(CMapWhoNode *node)	// FUNCTION: 0x00491d80
{
	mIterator = node->mFirst;
	return mIterator;
}

CMapWhoEntry *CMapWho::GetNextEntry()	// FUNCTION: 0x00491d90
{
	if (mIterator)
		mIterator = mIterator->mNext;
	return mIterator;
}

BOOL CMapWho::IsValidSector(const CSector &sector)	// FUNCTION: 0x00491da0
{
	if (sector.mLayer < 0 || sector.mLayer >= 5 || sector.mX < 0 ||
		sector.mX >= (64 >> (4 - sector.mLayer)) || sector.mY < 0 ||
		sector.mY >= (64 >> (4 - sector.mLayer)))
		return FALSE;
	return TRUE;
}

BOOL CMapWho::SetupNextRadiusLayer()	// FUNCTION: 0x00491df0
{
	mQueryLayer--;
	if (mQueryLayer < 0)
		return FALSE;
	SINT shift = mShift[mQueryLayer];
	float halfCell = (1 << shift) * 0.5f;
	mQueryMinX = ((SINT)(mQueryPos.X - mQueryRadius - halfCell)) >> shift;
	// STAND-IN (order; review F 2026-10-15): the X maximum's sum split through a named local, and the Y maximum's sum
	// grouped (half + radius) + Y; the plain `pos + radius + half` sums lose 00491df0 (either alone does).
	float maxX = mQueryPos.X;
	maxX += mQueryRadius;
	mQueryMaxX = ((SINT)(maxX + halfCell)) >> shift;
	mQueryMinY = ((SINT)(mQueryPos.Y - mQueryRadius - halfCell)) >> shift;
	mQueryMaxY = ((SINT)((halfCell + mQueryRadius) + mQueryPos.Y)) >> shift;
	mQuerySector = CSector((short)mQueryMinX, (short)mQueryMinY, mQueryLayer);
	return TRUE;
}

CMapWhoEntry *CMapWho::GetFirstEntryWithinRadius(FVector pos, float radius)	// FUNCTION: 0x00491ea0
{
	mQueryLayer = 5;
	mQueryPos = pos;
	mQueryRadius = radius;
	SetupNextRadiusLayer();
	mQueryActive = TRUE;
	CMapWhoEntry *entry = NULL;
	// An invalid first sector leaves the shared iterator untouched, as retail does.
	if (IsValidSector(mQuerySector))
		entry = GetFirstEntry(GetSector(mQuerySector));
	if (!entry)
		entry = AdvanceRadiusIterator();
	return entry;
}

CMapWhoEntry *CMapWho::GetNextEntryWithinRadius()	// FUNCTION: 0x00492020
{
	CMapWhoEntry *entry;
	if (!mQueryActive)
	{
		LOG.AddMessage("WARNING:  GetNextEntryWithinRadius not set up");
		entry = NULL;
	}
	else
		entry = AdvanceRadiusIterator();
	return entry;
}

// The same traversal is inlined at 00491f4d and 00492042; the helper name is provisional.
inline CMapWhoEntry *CMapWho::AdvanceRadiusIterator()
{
	CMapWhoEntry *entry = GetNextEntry();
	while (!entry)
	{
		mQuerySector.mX++;
		if (mQuerySector.mX > mQueryMaxX)
		{
			mQuerySector.mY++;
			if (mQuerySector.mY <= mQueryMaxY)
				mQuerySector.mX = mQueryMinX;
			else if (!SetupNextRadiusLayer())
			{
				mQueryActive = FALSE;
				return NULL;
			}
		}
		if (IsValidSector(mQuerySector))
			entry = GetFirstEntry(GetSector(mQuerySector));
	}
	return entry;
}

CMapWhoEntry *CMapWho::GetFirstEntryWithinLine(FVector start, FVector end)	// FUNCTION: 0x00492110
{
	float bounds[6];
	bounds[0] = 0;
	bounds[2] = 512;
	bounds[1] = 0;
	bounds[3] = 512;
	bounds[4] = -10000;
	bounds[5] = 10000;
	ClipSegmentAgainstAABB3D(&start.X, &start.Y, &start.Z, &end.X, &end.Y, &end.Z, bounds);
	mQueryLayer = 5;
	mLineStart = start;
	mLineEnd = end;
	SetupNextLineLayer();
	mQueryActive = TRUE;
	CMapWhoEntry *entry = NULL;
	if (IsValidSector(mLineSector))
		entry = GetFirstEntry(GetSector(mLineSector));
	if (!entry)
		entry = AdvanceLineEntryIterator();
	return entry;
}

BOOL CMapWho::SetupNextLineLayer()	// FUNCTION: 0x004922f0
{
	mQueryLayer--;
	if (mQueryLayer < 0)
		return FALSE;
	mLineSide = 0;
	mLineStep = 0;
	float cellSize = (float)(1 << mShift[mQueryLayer]);
	FVector delta = mLineEnd - mLineStart;
	if (delta.X == 0 && delta.Y == 0)
	{
		mLineStepX = cellSize;
		mLineStepY = 0;
		mLineStepLimit = 2;
		mLineXMajor = TRUE;
	}
	else
	{
		float absX = fabsf(delta.X);
		float absY = fabsf(delta.Y);
		if (absX > absY)
		{
			mLineXMajor = TRUE;
			if (delta.X > 0)
				mLineStepX = cellSize;
			else
				mLineStepX = -cellSize;
			mLineStepY = (delta.Y / absX) * cellSize;
			mLineStepLimit = (SINT)floor(absX / cellSize) + 2;
		}
		else
		{
			mLineXMajor = FALSE;
			mLineStepY = delta.Y > 0 ? cellSize : -cellSize;
			mLineStepX = (delta.X / absY) * cellSize;
			mLineStepLimit = (SINT)floor(absY / cellSize) + 2;
		}
	}
	mLinePosition = mLineStart;
	mLinePosition.X -= mLineStepX;
	mLinePosition.Y -= mLineStepY;
	mLineBaseSector = WorldToSector(mLinePosition, mQueryLayer);
	mLineSector = mLineBaseSector;
	return TRUE;
}

// STAND-IN (review F 2026-10-15): the sector edits go through a reference `CSector &s = mLineSector;`, so retail reloads mLineSide
// after them (004924f5/0049250c); the direct member form was not buildable here, the reference stays labelled.
BOOL CMapWho::AdvanceLineSector()	// FUNCTION: 0x004924b0
{
	mLineSide++;
	CSector	&s = mLineSector;
	s = mLineBaseSector;
	if (mLineSide == 1)
	{
		if (mLineXMajor)
			s.mY++;
		else
			s.mX--;
	}
	if (mLineSide == 2)
	{
		if (mLineXMajor)
			s.mY--;
		else
			s.mX++;
	}
	if (mLineSide == 3)
	{
		mLineStep++;
		if (mLineStep > mLineStepLimit)
			return FALSE;
		mLinePosition.X += mLineStepX;
		mLinePosition.Y += mLineStepY;
		mLineBaseSector = WorldToSector(mLinePosition, mQueryLayer);
		mLineSector = mLineBaseSector;
		mLineSide = 0;
	}
	return TRUE;
}

CMapWhoEntry *CMapWho::GetNextEntryWithinLine()	// FUNCTION: 0x004925a0
{
	CMapWhoEntry *entry;
	if (!mQueryActive)
	{
		LOG.AddMessage("WARNING:  GetNextEntryWithinLine not set up");
		entry = NULL;
	}
	else
		entry = AdvanceLineEntryIterator();
	return entry;
}

// The common line-entry walk is inlined in both public queries; its name is provisional.
inline CMapWhoEntry *CMapWho::AdvanceLineEntryIterator()
{
	CMapWhoEntry *entry = GetNextEntry();
	while (!entry)
	{
		if (!AdvanceLineSector() && !SetupNextLineLayer())
		{
			mQueryActive = FALSE;
			return NULL;
		}
		if (IsValidSector(mLineSector))
			entry = GetFirstEntry(GetSector(mLineSector));
	}
	return entry;
}

CSector CMapWho::WorldToSector(const FVector &pos, SINT layer)	// FUNCTION: 0x00492670
{
	SINT x = ((SINT)pos.X) >> mShift[layer];
	SINT y = ((SINT)pos.Y) >> mShift[layer];
	if (x >= mWidth[layer])
		x = mWidth[layer] - 1;
	if (x < 0)
		x = 0;
	if (y >= mHeight[layer])
		y = mHeight[layer] - 1;
	if (y < 0)
		y = 0;
	return CSector(x, y, layer);
}

void CMapWho::SortEntries()	// FUNCTION: 0x004926e0
{
	SINT width = 64;
	SINT height = 64;
	for (SINT layer = 4; layer >= 1; layer--)
	{
		for (SINT x = 0; x < width; x++)
		{
			for (SINT y = 0; y < height; y++)
			{
				CSector sector(x, y, layer);
				if (!IsValidSector(sector))
					LOG.AddMessage("FATAL ERROR: invalid sector in mapwho sort");
				CMapWhoNode *node = GetSector(sector);
				CMapWhoEntry *tail = node->mFirst;
				while (tail)
				{
					if (tail->mNext)
						tail = tail->mNext;
					else
						break;
				}
				if (tail)
				{
					CMapWhoEntry *prev = NULL;
					CMapWhoEntry *entry = node->mFirst;
					CMapWhoEntry *originalTail = tail;
					// The original tail is excluded. Earlier trees move behind it in encounter order.
					while (entry && entry != originalTail && entry->mNext)
					{
						CThing *thing = entry->GetThing();
						CMapWhoEntry *next = entry->mNext;
						if (thing && thing->IsA(THING_TYPE_TREE))
						{
							if (prev)
								prev->mNext = next;
							else
								node->mFirst = next;
							if (next)
								next->mPrev = prev;
							entry->mNext = NULL;
							entry->mPrev = tail;
							tail->mNext = entry;
							tail = entry;
						}
						else
							prev = entry;
						entry = next;
					}
				}
			}
		}
		width >>= 1;
		height >>= 1;
	}
}

//******************************************************************************************
// The size converted once: the named float puts y's product ahead of half's fmul, as in retail (00492898); the
// parenthesised terms give the rest.
// FUNCTION: 0x00492860
void	CMapWho::DrawSectorDebug(CSector s)
{
	SINT	size = 1 << mShift[s.mLayer];
	float	fsize = float(size);
	float	half = fsize * 0.5f;
	FVector	pos((float(s.mX * size)) + half, (float(s.mY * size)) + half, 0);
	FVector	axes = FVector(half, half, half * 15.f);

	DWORD	col;
	if (s.mLayer == 0)
		col = 0xff00ff20;
	else if (s.mLayer == 1)
		col = 0xffff0050;
	else if (s.mLayer == 2)
		col = 0xff2000ff;
	else if (s.mLayer == 3)
		col = 0xff309030;
	else if (s.mLayer == 4)
		col = 0xff00ffff;
	else if (s.mLayer == 5)
		col = 0xffff00ff;

	ENGINE.DrawDebugCuboid(col, axes, pos, ID_FMATRIX, ENGINE.GetOutlineTexture());
}

//******************************************************************************************
// Retail's tree test is (type & TREE) == 1, never true, so every occupied sector draws.
// FUNCTION: 0x00492950
void	CMapWho::DrawMapWhoDebug()
{
	ENGINE.DisableLighting();
	RENDERINFO.SetWorld(FVector(0, 0, 0), ID_FMATRIX);

	SINT	sx = 64, sy = 64;
	for (SINT layer = 4; layer >= 0; layer--)
	{
		for (SINT y = 0; y < sy; y++)
		{
			for (SINT x = 0; x < sx; x++)
			{
				CSector	s(x, y, layer);
				if (IsValidSector(s))
				{
					CMapWhoEntry	*e = GetSector(s)->mFirst;
					while (e && (e->GetThing()->IsA(THING_TYPE_TREE) == TRUE))
						e = e->mNext;
					if (e)
						DrawSectorDebug(CSector(x, y, layer));
				}
			}
		}
		sx >>= 1;
		sy >>= 1;
	}
}
