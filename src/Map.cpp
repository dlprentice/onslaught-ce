// Map.cpp: retail map lifecycle, sampled line query and cell-height bounds.
#include "common.h"
#define MAP_CPP
#include "map.h"
#include "Line.h"
#include "mathsconst.h"
#include "DebugLog.h"
#include "console.h"
#include "engine.h"
#include "mixermap.h"
#include "landscape.h"
#include "chunker.h"

// The map and level directories: retail's object starts its .data with these (0062d948, 0062d954) and nothing in
// the executable reads them. The names are ours.
char	MAP_DIRECTORY[] = "data\\maps\\";					// 0062d948 (name ours)
char	LEVEL_DIRECTORY[] = "data\\levels\\";				// 0062d954 (name ours)

#line 14
// FUNCTION: 0x00490920 init:ZERO_FVECTOR
// FUNCTION: 0x00490940 init:ID_FMATRIX

CMap MAP;
// FUNCTION: 0x00490a10 init:MAP
// FUNCTION: 0x00490a30 exit:MAP

// The quick-collision test of one sample point against the 64x64 cell heights (InitQuickCollisionMap 00490e30).
static inline BOOL HitsCell(CMap *map, const FVector &pos)
{
	// 00490bc4..00490bf0 uses float bits, not a numeric int cast. The exact bias is 3afff2e5.
	float packedX = (pos.X - (0.4999f / 256)) + 49152.0f;
	float packedY = (pos.Y - (0.4999f / 256)) + 49152.0f;
	SINT x = *(SINT *)&packedX;
	SINT y = *(SINT *)&packedY;
	if (((x | y) & 0x3e0000) == 0)
	{
		CMap::CellHeights *cell = &map->mCellHeights[((y >> 5) & 0xfc0) + ((x >> 11) & 0x3f)];
		if (pos.Z >= cell->mMinZ)
		{
			if (pos.Z > cell->mMaxZ)
				return TRUE;
			float scale = map->mHeightScale;
			// 00490c4c narrows the packed sampler's result to signed AX before scaling.
			return (SWORD)map->Unknown47eb00(x, y) * scale < pos.Z;
		}
		return FALSE;
	}
	return pos.Z > 0;
}

// `checks` is set after the length: a statement between the square root's store and its next use gives retail's
// `fstp [length]; fld [length]`, where `checks` first keeps the extended value on the FPU stack. The recursion's result
// is returned as it is (retail jumps to the epilogue with eax untouched). The cell test is the inline helper above: its
// expansion spends enough of the inline budget that the CLine of the finer trace calls FVector(0,0,0) out of line for
// its base (00490d1e), and position's X and Y, read only through the helper's reference, are numbered after direction's,
// so `position += direction` adds X and Y position-first and Z direction-first, as retail does (operand order follows
// symbol ids; the rank tool).
// FUNCTION: 0x00490a40
BOOL CMap::Unknown490a40(CLine *line, FVector *hit, SINT includeWater)
{
	float water = mWaterLevel;
	FVector direction = line->mEnd - line->mStart;
	float length = direction.Magnitude();
	SINT checks = 11;
	float step = length / 10.0f;
	if (step > 2.0f)
	{
		step = 2.0f;
		checks = (SINT)floor(length * 0.5f) + 1;
	}
	direction.Normalise();
	direction *= step;
	FVector position = line->mStart;
	SINT check;
	// The inclusive bound and the eleven initial steps can inspect beyond the original endpoint.
	for (check = 0; check <= checks; ++check)
	{
		BOOL collision = includeWater == TRUE && position.Z >= water;
		if (!collision)
			collision = HitsCell(this, position);
		if (collision)
		{
			if (step <= 0.01f)
			{
				*hit = position;
				return TRUE;
			}
			CLine smaller(position - direction, position);
			BOOL result = Unknown490a40(&smaller, hit, includeWater);
			if (result)
				return result;
			LOG.AddMessage("Warning: LOS return FALSE in lower step l = %2.8f, step = %2.8f checks = %d on check = %d",
				  length, step, checks, check);
			*hit = position;
			return TRUE;
		}
		position += direction;
	}
	return FALSE;
}

// FUNCTION: 0x00490e10
CMap::CMap()
{
}

// FUNCTION: 0x00490e20
CMap::~CMap()
{
}

// FUNCTION: 0x00490e30
void CMap::InitQuickCollisionMap()
{
	CellHeights *cell = mCellHeights;
	for (SINT y = 0; y < 512; y += 8)
	{
		for (SINT x = 0; x < 512; x += 8)
		{
			SWORD low = 32767;
			SWORD high = -32768;
			for (SINT dy = 0; dy <= 8; ++dy)
			{
				for (SINT dx = 0; dx <= 8; ++dx)
				{
					SWORD height = GetHeightSample(x + dx, y + dy);
					if (height < low) low = height;
					if (height > high) high = height;
				}
			}
			cell->mMaxZ = GetHeightScale() * high;
			cell->mMinZ = GetHeightScale() * low;
			++cell;
		}
	}
}

//******************************************************************************************
BOOL CMap::Init()	// FUNCTION: 0x00490f10
{
	if (!CHeightField::Init())
		return FALSE;
	mGeometryLoaded = FALSE;
	mPropertiesLoaded = FALSE;
	return TRUE;
}

//******************************************************************************************
void CMap::Shutdown()	// FUNCTION: 0x00490f40
{
	CHeightField::Shutdown();
	MIXERMAP.Destroy();
}

//******************************************************************************************
BOOL CMap::AlreadyLoaded(SINT number, BOOL geometry, BOOL properties)	// FUNCTION: 0x00490f50
{
	char	text[64];
	sprintf(text, "Loading map %d", number);
	if (properties)
	{
		if (geometry)
			strcat(text, " (all)\n");
		else
			strcat(text, " (properties only)\n");
	}
	else
	{
		if (geometry)
			strcat(text, " (geometry only)\n");
		else
			strcat(text, " (nothing?!)\n");
	}
	TRACE(text);

	if (number == -1)
		return TRUE;

	if (geometry)
	{
		if (mGeometryLoaded)
		{
			TRACE("Map geometry is already loaded - ignoring\n");
			return TRUE;
		}
	}
	else if (properties && mPropertiesLoaded)
	{
		TRACE("Map properties are already loaded - ignoring\n");
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
void CMap::Deserialize(CChunkReader *c)	// FUNCTION: 0x00491060
{
	CONSOLE.Status("Deserializing map");
	c->GetNext();
	SINT	number;
	c->Read(&number, 4, 1);

	char	text[32];
	sprintf(text, "Deserializing map %d\n", number);
	TRACE(text);

	mMapNumber = number;
	mPropertiesLoaded = TRUE;
	mGeometryLoaded = TRUE;

	CHeightField::Load(c);
	MIXERMAP.Init(c);
	ENGINE.SetKempyCube(mHFP.mUnknown18[0]);
	ENGINE.Unknown452b60(mHFP.mUnknown18[1]);
	ENGINE.LoadMixers(mUnknown1030[0]);
	CLandscape::LoadMixerTexture(mHFP.mUnknown18[4]);
	ENGINE.SetWater(mHFP.mUnknown18[5]);
	CONSOLE.StatusDone("Deserializing map", TRUE);
}

//******************************************************************************************
// An engine hook with nothing to do on the PC (engine.h). The empty one-argument body is folded into the shared one at
// 00452b60 (/OPT:ICF); auto-inlining is off around it because retail keeps the call above.
#pragma auto_inline(off)
// FUNCTION: 0x00452b60
void CEngine::Unknown452b60(UBYTE number)
{
}
#pragma auto_inline(on)
