// NavigationMap.cpp: reconstructed from NavigationMap.obj (004bc0b0..004bef9f); Stuart's release has no copy. The
// world's occupancy grids: 256 x 256 bits of two-unit cells, one grid per slope a unit can climb (35, 45 and 60
// degrees; CWorld::InitLODLists), the units standing on them, and the grid path search's cost table.

#include "common.h"
class CSphere;class CLine;class CGeneralVolume;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#define NAVIGATION_FULL
#include "mathsconst.h"
#include "world.h"
#include "unit.h"
#include "map.h"
#include "DXMemBuffer.h"
#include "Line.h"
#include "Sphere.h"
#include "collisionseekingthing.h"
// FUNCTION: 0x004bc0b0 init:ZERO_FVECTOR
// FUNCTION: 0x004bc0d0 init:ID_FMATRIX

SINT			COccupancyGrid::sMoves[500];
BOOL			COccupancyGrid::sEnabled;
SPtrSet<CUnit>	COccupancyGrid::sUnits;
// FUNCTION: 0x004bc1a0 init:sUnits
// FUNCTION: 0x004bc1c0 exit:sUnits

COccupancyGrid	STATIC_OCCUPANCY(0.0f);			// 00807580 (name open)
// FUNCTION: 0x004bc1e0 init:STATIC_OCCUPANCY
// FUNCTION: 0x004bc250 exit:STATIC_OCCUPANCY

SINT			COccupancyGrid::sEndY = 0;
SINT			COccupancyGrid::sEndX = 0;
COccupancyGrid	*COccupancyGrid::sGrid = NULL;
SINT			COccupancyGrid::sY = 0;
UWORD			COccupancyGrid::sCost[256][256] = {0};
SINT			COccupancyGrid::sX = 0;
SINT			COccupancyGrid::sMinX = 0;
SINT			COccupancyGrid::sMinY = 0;
SINT			COccupancyGrid::sMaxX = 255;
SINT			COccupancyGrid::sMaxY = 255;

#include "Occupancy.h"

//******************************************************************************************
COccupancyGrid::COccupancyGrid(float max_slope)	// FUNCTION: 0x004bc260
{
	sEnabled = FALSE;
	mMaxSlope = max_slope * (PI / 180.0f);
	memset(sCost, 0xff, sizeof(sCost));
	sMinX = 0xff;
	sMinY = 0xff;
	sMaxX = 0;
	sMaxY = 0;
	for (SINT y = 0; y < 256; y++)
		for (SINT x = 0; x < 32; x++)
			mData[x * 256 + y] = 0xff;
}

//******************************************************************************************
COccupancyGrid::~COccupancyGrid()	// FUNCTION: 0x004bc2d0
{
	sUnits.RemoveAll();
}

//******************************************************************************************
// Resets the part of the cost table the last search touched, then builds the path. The bounds are widened and clamped
// in the statics themselves: C2 keeps the four statics in registers for the function and the final resets make the
// intermediate stores dead. Because the column start is then a static (not a local copy) when the row loop is
// strength-reduced, only r * 256 becomes the row step (edx += 0x100, 004bc354) and the fill address is formed per row
// (lea eax,[edx+esi]; lea edi,[eax*2+sCost], 004bc33a). With local copies the whole fill address is the row step.
void COccupancyGrid::Unknown4bc2e0(FVector start, FVector end, SINT a, CNormalSquadUnknownD4 *path)	// FUNCTION: 0x004bc2e0
{
	// reset the part of the cost table the last search touched
	sMinX--;
	sMinY--;
	sMaxX++;
	sMaxY++;
	if (sMinX < 0)
		sMinX = 0;
	if (sMinY < 0)
		sMinY = 0;
	if (sMaxX > 256)
		sMaxX = 256;
	if (sMaxY > 256)
		sMaxY = 256;
	for (SINT r = sMinY; r < sMaxY; r++)
		for (SINT c = sMinX; c < sMaxX; c++)
			sCost[r][c] = 0xffff;

	sMinX = 0xff;
	sMinY = 0xff;
	sMaxX = 0;
	sMaxY = 0;
	BuildPath(start, end, this, a, path);
}

//******************************************************************************************
void COccupancy::RemoveUnit(CUnit *unit)	// FUNCTION: 0x004bc3e0
{
	COccupancyGrid::sUnits.Remove(unit);
	if (COccupancyGrid::sEnabled && WORLD.mOccupancy[0])
		Rasterize((SINT)(unit->GetPos().X - unit->GetRadius() - 1.0f), (SINT)(unit->GetPos().Y - unit->GetRadius() - 1.0f),
				  (SINT)(unit->GetRadius() + unit->GetPos().X + 1.0f), (SINT)(unit->GetRadius() + unit->GetPos().Y + 1.0f), TRUE);
}

//******************************************************************************************
void COccupancy::AddUnit(CUnit *unit)	// FUNCTION: 0x004bc480
{
	COccupancyGrid::sUnits.Add(unit);
	if (COccupancyGrid::sEnabled && WORLD.mOccupancy[0])
	{
		Rasterize((SINT)(unit->GetPos().X - unit->GetRadius()), (SINT)(unit->GetPos().Y - unit->GetRadius()),
				  (SINT)(unit->GetRadius() + unit->GetPos().X), (SINT)(unit->GetRadius() + unit->GetPos().Y), TRUE);
		BuildStaticShadows(unit);
	}
}

//******************************************************************************************
BOOL COccupancyGrid::IsBlocked(SINT x0, SINT y0, SINT x1, SINT y1)	// FUNCTION: 0x004bc510
{
	if (x0 < 0 || x0 > 255 || y0 < 0 || y0 > 255 || x1 < 0 || x1 > 255 || y1 < 0 || y1 > 255)
		return TRUE;

	// each axis is ordered on its own (so a falling line is walked as a rising one)
	SINT	t;
	if (x0 > x1)
	{
		t = x0;
		x0 = x1;
		x1 = t;
	}
	if (y0 > y1)
	{
		t = y0;
		y0 = y1;
		y1 = t;
	}

	SINT	dx = x1 - x0;
	SINT	dy = y1 - y0;
	if (dx || dy)
	{
		float	step = 0;
		SINT	x, y;
		if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy))
		{
			float	fy = float(y0);
			if (dx)
				step = float(dy) / dx;
			for (x = x0; x < x1 + 1; x++)
			{
				y = SINT(fy);
				if (!(mData[(x >> 3) * 256 + y] & (1 << (x % 8))))
					return TRUE;
				if (x == x1 && y == y1)
					break;
				fy += step;
			}
		}
		else
		{
			float	fx = float(x0);
			if (dy)
				step = float(dx) / dy;
			for (y = y0; y < y1 + 1; y++)
			{
				x = SINT(fx);
				if (x == x1 && y == y1)
					break;
				if (!(mData[(x >> 3) * 256 + y] & (1 << (x % 8))))
					return TRUE;
				fx += step;
			}
		}
	}
	return FALSE;
}

//******************************************************************************************
// Moves a blocked cell to the nearest open one: square rings round it, the top and bottom rows of each before its sides.
BOOL COccupancyGrid::FindNearest(SINT *px, SINT *py)	// FUNCTION: 0x004bc6d0
{
	for (SINT r = 0; r < 256; r++)
	{
		SINT	x, y;
		for (x = *px - r; x <= *px + r; x++)
		{
			if (x >= 0 && x < 256)
			{
				if (*py - r >= 0 && *py - r < 256 && TestCell(x, *py - r))
				{
					*px = x;
					*py -= r;
					return TRUE;
				}
				if (*py + r >= 0 && *py + r < 256 && TestCell(x, *py + r))
				{
					*px = x;
					*py += r;
					return TRUE;
				}
			}
		}
		for (y = *py - r; y <= *py + r; y++)
		{
			if (y >= 0 && y < 256)
			{
				if (*px - r >= 0 && *px - r < 256 && TestCell(*px - r, y))
				{
					*px -= r;
					*py = y;
					return TRUE;
				}
				if (*px + r >= 0 && *px + r < 256 && TestCell(*px + r, y))
				{
					*px += r;
					*py = y;
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

// Inline functions from world.h that this object emits.
// FUNCTION: 0x004be970 ?TestCell@COccupancyGrid@@QAEHHH@Z
// FUNCTION: 0x004be9b0 ?CanStepNorth@COccupancyGrid@@SAHXZ
// FUNCTION: 0x004bea10 ?CanStepWest@COccupancyGrid@@SAHXZ
// FUNCTION: 0x004bea70 ?CanStepSouth@COccupancyGrid@@SAHXZ
// FUNCTION: 0x004bead0 ?CanStepEast@COccupancyGrid@@SAHXZ

// The grids by number (inline; the occupancy builders' budgets count it).
static inline COccupancyGrid *GetGrid(SINT i) { return WORLD.mOccupancy[i]; }

//******************************************************************************************
// A cell under water closes on every grid; one too steep closes on each grid that cannot climb it.
void COccupancy::ClearSteep()	// FUNCTION: 0x004bc8d0
{
	float	water = MAP.GetWaterLevel();	// retail: copied through fld/fstp (map.h's GetWaterLevel)
	for (SINT y = 0; y < 511; y++)
		for (SINT x = 0; x < 511; x++)
		{
			FVector	pos(x + 0.5f, y + 0.5f, 0);
			if (MAP.GetHeight(SINT(pos.X), SINT(pos.Y)) > water)
			{
				GetGrid(0)->ClearCross(x, y);
				GetGrid(1)->ClearCross(x, y);
				GetGrid(2)->ClearCross(x, y);
			}
			else
			{
				FVector	n = MAP.CHeightField::Normal(pos);	// the height field directly: CMap's forwarding Normal would spend this function's inline budget
				float	slope = n.Elevation() + PI / 2;
				if (slope > GetGrid(0)->mMaxSlope)
					GetGrid(0)->ClearCross(x, y);
				if (slope > GetGrid(1)->mMaxSlope)
					GetGrid(1)->ClearCross(x, y);
				if (slope > GetGrid(2)->mMaxSlope)
					GetGrid(2)->ClearCross(x, y);
			}
		}
}

//******************************************************************************************
void COccupancy::ApplyStatic()	// FUNCTION: 0x004bcbf0
{
	SINT	x, y, i, j;
	for (j = 0, y = 0; y < 512; j++, y += 2)
		for (i = 0, x = 0; x < 512; i++, x += 2)
			if (!STATIC_OCCUPANCY.TestCell(i, j))
			{
				GetGrid(0)->SetCell(x, y, FALSE);
				GetGrid(1)->SetCell(x, y, FALSE);
				GetGrid(2)->SetCell(x, y, FALSE);
			}
	COccupancyGrid::sEnabled = TRUE;
}

//******************************************************************************************
// The occupancy builders' collision set-up, shared by BuildStatic and BuildStaticShadows. Written as inline helpers
// because retail's inline budget ran out where they add to it: the volumes' base constructors (0040b100), the
// report's normals (vector_constructor_iterator) and some ClearCross calls go out of line only with them.
// Retail initializes the three stopped flags before sampling the terrain, then copies line, sphere and unit
// movement in that order. The report constructor initializes its first scalar before the six normal records;
// the queries reset the other collision fields afterwards.
// STAND-IN (concession): ShadowHits owns the ordered line/sphere query and interprets both used BOOL results
// through CollisionHit. The names and wrapper structure are ours; Stuart's NavigationMap source is lost.
// Why (BuildStaticShadows): retail compares both full-width results with the live zero register (004bddbd,
// 004bdde8). Direct tests leave four differing bytes. Separate CollisionHit calls give those comparisons but expand
// the third ClearCross, changing ten SetCell calls plus one ClearCross into fifteen SetCell calls. How: the combined
// short-circuit query retains the result boundaries and the retail distribution; both helpers expand and emit no
// standalone call, field write or callback. Line is tested first and sphere only after a zero result; any nonzero
// BOOL remains a hit. Plain query with the corrected construction: four-byte miss. The complete section and every
// reference match with this wrapper; its original source spelling remains unknown. BuildStatic runs the same query
// through it (its budget fit is described there).
static inline void SetShadowMove(CCollisionMove &m, const FVector &move, const FVector &pos)
{
	m.mPos = pos;
	m.mMove = move;
}

static inline void ResetShadowReport(CCollisionReport &r)
{
	r.mNumContacts = 0;
	r.mUnknownA8 = 0;
	r.mStuck = FALSE;
	r.mTestTriangles = 0;
	r.mUnknownD0 = 1;
}

// Interpret the used BOOL returned by the collision API.
static inline BOOL CollisionHit(BOOL result) { return result != FALSE; }

// Own the actual ordered line/sphere collision query.
static inline BOOL ShadowHits(CGeneralVolume *shape, CCollisionMove &unitMove, CCollisionMove &lineMove,
		CLine &line, CCollisionMove &sphereMove, CSphere &sphere, CCollisionReport &report)
{
	return CollisionHit(shape->CollideWithLine(&unitMove, &lineMove, &line, &report)) ||
		CollisionHit(shape->CollideWithSphere(&unitMove, &sphereMove, &sphere, &report));
}

//******************************************************************************************
// The static mask opens again, then the cells each unit stands on close on it and on every grid: every cell across
// its radius whose ground its collision mesh reaches, down a line or with a unit sphere (all of them for a unit
// without one). The grids are then in use.
// Matched: the collision set-up is BuildStaticShadows' (stopped flags first, then the
// line, sphere and unit moves; the report constructor's first field not reset again), and the collision mesh is
// tested through its accessor and fetched again inside the test, so shape is defined after the branch: retail stores
// it there (004bd155) and keeps it in memory. Defined before the test (def and test in the 35-web body block), shape
// outranked zero and x + 1 (1344 against 1225 and 864) and took ebp, zero ebx, x + 1 none (89/497).
// The query goes through the shared ShadowHits stand-in (see the helpers above). Besides retail's `cmp eax,ebp`
// comparisons (004bd1b5, 004bd1e2) it fits the inline budget: the first ClearCross needs 595..713 left for five of the eight to expand and
// three to be called, as in retail. Through the wrapper (size 72, own size 725) 653 is left; with the query written
// out, own size 743 leaves 761 (31/39), and each result through CollisionHit 751 leaves 777 (27/35): six expand.
void COccupancy::BuildStatic()	// FUNCTION: 0x004bcd60
{
	for (SINT j = 0; j < 256; j++)
		for (SINT i = 0; i < 32; i++)
			STATIC_OCCUPANCY.mData[i * 256 + j] = 0xff;

	for (CUnit *unit = COccupancyGrid::sUnits.First(); unit; unit = COccupancyGrid::sUnits.Next())
	{
		float	radius = unit->GetBoundingRadius();
		FVector	centre;
		unit->GetCentrePos(centre);
		for (SINT y = (SINT)floor(centre.Y - radius); y < (SINT)ceil(centre.Y + radius); y++)
		{
			for (SINT x = (SINT)floor(centre.X - radius); x < (SINT)ceil(centre.X + radius); x++)
			{
				CLine	line(FVector(0, 0, -100.0f), FVector(0, 0, 0));
				CSphere	sphere(1.0f);
				CCollisionMove	unitMove;
				unitMove.mStopped = FALSE;
				CCollisionMove	sphereMove;
				sphereMove.mStopped = FALSE;
				CCollisionMove	lineMove;
				lineMove.mStopped = FALSE;
				FVector	pos(x + 0.5f, y + 0.5f, 0);
				pos.Z = MAP.GetHeight(SINT(pos.X), SINT(pos.Y));

				SetShadowMove(lineMove, FVector(0, 0, 0), pos);
				SetShadowMove(sphereMove, FVector(0, 0, 0), pos);
				SetShadowMove(unitMove, FVector(0, 0, 0), unit->GetPos());

				if (unit->GetCST()->GetCollisionMeshVolume())
				{
					CGeneralVolume	*shape = unit->GetCST()->GetCollisionMeshVolume();
					CCollisionReport	report;
					ResetShadowReport(report);
					if (ShadowHits(shape, unitMove, lineMove, line, sphereMove, sphere, report))	// STAND-IN (see above)
					{
						STATIC_OCCUPANCY.ClearCross(x, y);
						GetGrid(0)->ClearCross(x, y);
						GetGrid(1)->ClearCross(x, y);
						GetGrid(2)->ClearCross(x, y);
					}
				}
				else
				{
					STATIC_OCCUPANCY.ClearCross(x, y);
					GetGrid(0)->ClearCross(x, y);
					GetGrid(1)->ClearCross(x, y);
					GetGrid(2)->ClearCross(x, y);
				}
			}
		}
	}
	COccupancyGrid::sEnabled = TRUE;
}

// FUNCTION: 0x004bd440 ?ClearCross@COccupancyGrid@@QAEXHH@Z

//******************************************************************************************
// Opens a box of cells on every grid and closes again those under water or too steep; unless told not to, it then
// lays every unit's static shadow back down. The cell test is ClearSteep's, written out again (a shared helper sits
// one call deeper, where the budget denies more than retail did).
void COccupancy::Rasterize(SINT x0, SINT y0, SINT x1, SINT y1, BOOL a)	// FUNCTION: 0x004bd5c0
{
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > 511)
		x1 = 511;
	if (y1 > 511)
		y1 = 511;

	SINT	x, y;
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
		{
			GetGrid(0)->SetCell(x, y, TRUE);
			GetGrid(1)->SetCell(x, y, TRUE);
			GetGrid(2)->SetCell(x, y, TRUE);
		}

	float	water = MAP.GetWaterLevel();	// retail: copied through fld/fstp (map.h's GetWaterLevel)
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
		{
			FVector	pos(x + 0.5f, y + 0.5f, 0);
			if (MAP.GetHeight(SINT(pos.X), SINT(pos.Y)) > water)
			{
				GetGrid(0)->ClearCross(x, y);
				GetGrid(1)->ClearCross(x, y);
				GetGrid(2)->ClearCross(x, y);
			}
			else
			{
				FVector	n = MAP.CHeightField::Normal(pos);	// as at line 185
				float	slope = n.Elevation() + PI / 2;
				if (slope > GetGrid(0)->mMaxSlope)
					GetGrid(0)->ClearCross(x, y);
				if (slope > GetGrid(1)->mMaxSlope)
					GetGrid(1)->ClearCross(x, y);
				if (slope > GetGrid(2)->mMaxSlope)
					GetGrid(2)->ClearCross(x, y);
			}
		}

	if (!a)
	{
		CUnit	*unit;
		for (unit = COccupancyGrid::sUnits.First(); unit; unit = COccupancyGrid::sUnits.Next())
			BuildStaticShadows(unit);
	}
	COccupancyGrid::sEnabled = TRUE;
}

//******************************************************************************************
// The cells a unit stands on close on every grid: across its radius, every other cell whose ground its collision
// mesh reaches, down a line from the ground or with a unit sphere there.
// The collision set-up and the query wrapper (a labelled stand-in) are the shared helpers above BuildStatic.
void COccupancy::BuildStaticShadows(CUnit *unit)	// FUNCTION: 0x004bd9e0
{
	float	radius = unit->GetBoundingRadius();
	FVector	centre;
	unit->GetCentrePos(centre);
	for (SINT y = (SINT)floor(centre.Y - radius); y < (SINT)ceil(centre.Y + radius); y += 2)
	{
		for (SINT x = (SINT)floor(centre.X - radius); x < (SINT)ceil(centre.X + radius); x += 2)
		{
			CLine	line(FVector(0, 0, -100.0f), FVector(0, 0, 0));
			CSphere	sphere(1.0f);
			CCollisionMove unitMove;
			unitMove.mStopped = FALSE;
			CCollisionMove sphereMove;
			sphereMove.mStopped = FALSE;
			CCollisionMove lineMove;
			lineMove.mStopped = FALSE;
			FVector	pos(x + 1.0f, y + 1.0f, 0);
			pos.Z = MAP.Collide(pos);

			SetShadowMove(lineMove, FVector(0, 0, 0), pos);
			SetShadowMove(sphereMove, FVector(0, 0, 0), pos);
			SetShadowMove(unitMove, FVector(0, 0, 0), unit->GetPos());

			CGeneralVolume	*shape = unit->GetCST()->GetCollisionMeshVolume();
			CCollisionReport	report;
			ResetShadowReport(report);

			if (ShadowHits(shape, unitMove, lineMove, line, sphereMove, sphere, report))
			{
				GetGrid(0)->ClearCross(x, y);
				GetGrid(1)->ClearCross(x, y);
				GetGrid(2)->ClearCross(x, y);
			}
		}
	}
}

// Defined here, after BuildStaticShadows, where retail emits its out-of-line copy (world.h declares it).
// FUNCTION: 0x004bdf70
inline void COccupancyGrid::SetCell(SINT x, SINT y, BOOL set)
{
	x >>= 1;
	y >>= 1;
	if (x < 0 || x >= 256 || y < 0 || y >= 256)
		return;
	if (set)
		mData[(x >> 3) * 256 + y] |= 1 << (x % 8);
	else
		mData[(x >> 3) * 256 + y] &= 0xff - (1 << (x % 8));
}

//******************************************************************************************
void COccupancyGrid::SkipLegacy(CDXMemBuffer &file)	// FUNCTION: 0x004bdff0
{
	SINT	version;
	UBYTE	b;
	SINT	n;
	file.Read(&version, 4);
	switch (version)
	{
	case 1:
		for (n = 0x8000; n; n--)
			file.Read(&b, 1);
		break;
	case 2:
		for (n = 0x2000; n; n--)
			file.Read(&b, 1);
		break;
	}
}

//******************************************************************************************
// Legacy rows use world coordinates; decode their two-unit grid row once before reading its packed bytes.
// The per-bit writer retains SetCell's coordinate bounds and set/clear masks. b stays a SINT whose low byte is
// read: retail's frame puts it after y and version. The decoder structure and local bindings are reconstruction;
// no original source spelling was recovered. Stock VC6 matches the full section and all three Read references.
void COccupancyGrid::Load(CDXMemBuffer &file)	// FUNCTION: 0x004be050
{
	SINT	version;
	SINT	x, y, i, j;
	file.Read(&version, 4);
	switch (version)
	{
	case 1:
		for (y = 0; y < 512; y++)
		{
			const SINT gridY = y >> 1;
			for (x = 0; x < 512; x += 8)
			{
				SINT	b;
				file.Read(&b, 1);
				for (SINT bit = 0; bit < 8; bit++)
				{
					BOOL	set = ((UBYTE)b >> bit) & 1;
					{
						const SINT &xRef = x;
						const BOOL &setRef = set;
						SINT cellX = xRef + bit;
						cellX >>= 1;
						if (cellX >= 0 && cellX < 256 && gridY >= 0 && gridY < 256)
						{
							if (setRef)
								mData[(cellX >> 3) * 256 + gridY] |= 1 << (cellX % 8);
							else
								mData[(cellX >> 3) * 256 + gridY] &= 0xff - (1 << (cellX % 8));
						}
					}
				}
			}
		}
		break;
	case 2:
		for (i = 0; i < 256; i++)
			for (j = 0; j < 32; j++)
				file.Read(&mData[j * 256 + i], 1);
		break;
	}
}

//******************************************************************************************
void COccupancyGrid::SkipHeader(CDXMemBuffer &file)	// FUNCTION: 0x004be170
{
	SINT	a, b, c, d, e;
	file.Read(&a, 4);
	file.Read(&b, 4);
	file.Read(&c, 4);
	file.Read(&d, 4);
	file.Read(&e, 4);
}

//******************************************************************************************
// The grid path from start to end: straight there when nothing blocks the line, else a flood of costs out from the
// start cell and a walk down them from the end, simplified by line of sight.
void COccupancyGrid::BuildPath(FVector start, FVector end, COccupancyGrid *grid, SINT a, CNormalSquadUnknownD4 *path)	// FUNCTION: 0x004be1d0
{
	SINT	sx = SINT(start.X) >> 1;
	SINT	sy = SINT(start.Y) >> 1;
	sGrid = grid;
	SINT	ex = SINT(end.X);
	SINT	ey;
	sEndX = ex >> 1;
	ey = SINT(end.Y);
	sEndY = ey >> 1;
	path->mUnknown00 = ex;
	path->mUnknown04 = ey;
	path->mUnknown08 = 0;
	path->mUnknown0C = 0;

	if (!grid->IsBlocked(sx, sy, sEndX, sEndY))
	{
		path->AddCell(sEndX * 2 + 1, sEndY * 2 + 1);
		path->mUnknown08 = 1;
		return;
	}

	if (!grid->FindNearest(&sEndX, &sEndY))
		return;
	if (!grid->FindNearest(&sx, &sy))
		return;

	sX = sx;
	sY = sy;
	sMinY = sx;
	sMinX = sy;
	sMaxX = sy + 1;
	sMaxY = sx + 1;
	if (!Flood())
		FindNearestVisited();

	SINT	x = sEndX;
	SINT	y = sEndY;
	SINT	px, py;
	do
	{
		px = x;
		py = y;
		path->AddCell(x * 2 + 1, y * 2 + 1);
		StepDown(&x, &y);
		if (x == px && y == py)
			return;
	} while (x != sx || y != sy);
	path->Simplify(sGrid);
	path->mUnknown08 = 1;
}

//******************************************************************************************
// The step from (sX, sY) toward the end (1 west, 2 north, 3 east, 4 south; 0 none): along the longer axis first, then
// the other axis toward the end, then away from it, then back along the longer axis.
static inline SINT Abs(SINT v)
{
	return (v < 0) ? -v : v;
}

static inline SINT ChooseStep()
{
	if (Abs(COccupancyGrid::sX - COccupancyGrid::sEndX) > Abs(COccupancyGrid::sY - COccupancyGrid::sEndY))
	{
		if (COccupancyGrid::sX > COccupancyGrid::sEndX)
		{
			if (COccupancyGrid::CanStepNorth())
				return 2;
			if (COccupancyGrid::sY > COccupancyGrid::sEndY)
			{
				if (COccupancyGrid::CanStepWest())
					return 1;
				if (COccupancyGrid::CanStepEast())
					return 3;
			}
			else
			{
				if (COccupancyGrid::CanStepEast())
					return 3;
				if (COccupancyGrid::CanStepWest())
					return 1;
			}
			if (COccupancyGrid::CanStepSouth())
				return 4;
		}
		else
		{
			if (COccupancyGrid::CanStepSouth())
				return 4;
			if (COccupancyGrid::sY > COccupancyGrid::sEndY)
			{
				if (COccupancyGrid::CanStepWest())
					return 1;
				if (COccupancyGrid::CanStepEast())
					return 3;
			}
			else
			{
				if (COccupancyGrid::CanStepEast())
					return 3;
				if (COccupancyGrid::CanStepWest())
					return 1;
			}
			if (COccupancyGrid::CanStepNorth())
				return 2;
		}
	}
	else
	{
		if (COccupancyGrid::sY > COccupancyGrid::sEndY)
		{
			if (COccupancyGrid::CanStepWest())
				return 1;
			if (COccupancyGrid::sX > COccupancyGrid::sEndX)
			{
				if (COccupancyGrid::CanStepNorth())
					return 2;
				if (COccupancyGrid::CanStepSouth())
					return 4;
			}
			else
			{
				if (COccupancyGrid::CanStepSouth())
					return 4;
				if (COccupancyGrid::CanStepNorth())
					return 2;
			}
			if (COccupancyGrid::CanStepEast())
				return 3;
		}
		else
		{
			if (COccupancyGrid::CanStepEast())
				return 3;
			if (COccupancyGrid::sX > COccupancyGrid::sEndX)
			{
				if (COccupancyGrid::CanStepNorth())
					return 2;
				if (COccupancyGrid::CanStepSouth())
					return 4;
			}
			else
			{
				if (COccupancyGrid::CanStepSouth())
					return 4;
				if (COccupancyGrid::CanStepNorth())
					return 2;
			}
			if (COccupancyGrid::CanStepWest())
				return 1;
		}
	}
	return 0;
}

//******************************************************************************************
// A depth-first walk from (sX, sY) toward the end, numbering each cell it enters with its depth and backing up from dead
// ends; it gives up after 500 cells. Near miss: its structure, loop and out-of-line calls follow retail's, but ours
// inlines nine of the step checks where retail inlines eight (the inline budget, README).
BOOL COccupancyGrid::Flood()	// FUNCTION: 0x004be420
{
	SINT	depth = 0;
	SINT	steps = 0;
	for (;;)
	{
		steps++;
		depth++;
		sCost[sX][sY] = depth;
		if (sX == sEndX && sY == sEndY)
			return TRUE;
		for (;;)
		{
			if (steps >= 500)
				return FALSE;
			switch (ChooseStep())
			{
			case 1:
				sY--;
				if (sY < sMinX)
					sMinX = sY;
				sMoves[depth] = 1;
				break;
			case 2:
				sX--;
				if (sX < sMinY)
					sMinY = sX;
				sMoves[depth] = 2;
				break;
			case 3:
				sY++;
				if (sY + 1 > sMaxX)
					sMaxX = sY + 1;
				sMoves[depth] = 3;
				break;
			case 4:
				sX++;
				if (sX + 1 > sMaxY)
					sMaxY = sX + 1;
				sMoves[depth] = 4;
				break;
			case 0:
				if (depth <= 1)
					return FALSE;
				depth--;
				switch (sMoves[depth])
				{
				case 1:
					sY++;
					continue;
				case 3:
					sY--;
					continue;
				case 2:
					sX++;
					continue;
				case 4:
					sX--;
					continue;
				}
				return FALSE;
			}
			break;
		}
	}
}

//******************************************************************************************
// When the walk cannot reach the end, the end moves to the nearest cell it did reach: square rings round the end, the
// top and bottom rows of each before its sides. Near miss: the ring's bounds are strength-reduced differently.
void COccupancyGrid::FindNearestVisited()	// FUNCTION: 0x004beb30
{
	for (SINT r = 0; r < 256; r++)
	{
		SINT	x, y;
		for (x = sEndX - r; x <= sEndX + r; x++)
		{
			if (x >= 0 && x < 256)
			{
				if (sEndY - r >= 0 && sEndY - r < 256 && sCost[x][sEndY - r] != 0xffff)
				{
					sEndX = x;
					sEndY -= r;
					return;
				}
				if (sEndY + r >= 0 && sEndY + r < 256 && sCost[x][sEndY + r] != 0xffff)
				{
					sEndX = x;
					sEndY += r;
					return;
				}
			}
		}
		for (y = sEndY - r; y <= sEndY + r; y++)
		{
			if (y >= 0 && y < 256)
			{
				if (sEndX - r >= 0 && sEndX - r < 256 && sCost[sEndX - r][y] != 0xffff)
				{
					sEndX -= r;
					sEndY = y;
					return;
				}
				// Retail 004bec65 uses the completed top/bottom loop's x here, not r.
				// Its bounds check and the returned endpoint still use the radius.
				if (sEndX + r >= 0 && sEndX + r < 256 && sCost[sEndX + x][y] != 0xffff)
				{
					sEndX += r;
					sEndY = y;
					return;
				}
			}
		}
	}
}

//******************************************************************************************
void COccupancyGrid::StepDown(SINT *px, SINT *py)	// FUNCTION: 0x004bed30
{
	SINT	x = *px;
	SINT	y = *py;
	SINT	best = sCost[x][y];
	SINT	bx = x;
	SINT	by = y;

	if (x > 0)
		if (sCost[x - 1][y] < best)
		{
			best = sCost[x - 1][y];
			bx = x - 1;
		}
	if (x < 255)
		if (sCost[x + 1][y] < best)
		{
			best = sCost[x + 1][y];
			bx = x + 1;
			by = y;
		}
	if (y > 0)
		if (sCost[x][y - 1] < best)
		{
			best = sCost[x][y - 1];
			bx = x;
			by = y - 1;
		}
	if (y < 255)
		if (sCost[x][y + 1] < best)
		{
			best = sCost[x][y + 1];
			bx = x;
			by = y + 1;
		}
	if (x > 0)
		if (y > 0)
			if (sCost[x - 1][y - 1] < best)
			{
				best = sCost[x - 1][y - 1];
				bx = x - 1;
				by = y - 1;
			}
	if (x < 255)
		if (y > 0)
			if (sCost[x + 1][y - 1] < best)
			{
				best = sCost[x + 1][y - 1];
				bx = x + 1;
				by = y - 1;
			}
	if (x > 0)
		if (y < 255)
			if (sCost[x - 1][y + 1] < best)
			{
				best = sCost[x - 1][y + 1];
				bx = x - 1;
				by = y + 1;
			}
	if (x < 255)
		if (y < 255)
			if (sCost[x + 1][y + 1] < best)
			{
				best = sCost[x + 1][y + 1];
				bx = x + 1;
				by = y + 1;
			}
	*px = bx;
	*py = by;
}

//******************************************************************************************
void CNormalSquadUnknownD4::Simplify(COccupancyGrid *grid)	// FUNCTION: 0x004beea0
{
	SINT	i, j, remove;

	// the furthest cell the last one can see
	for (i = mUnknown0C - 1; i >= 0; i--)
		if (grid->IsBlocked(mUnknown10[mUnknown0C - 1], mUnknown18[mUnknown0C - 1], mUnknown10[i], mUnknown18[i]))
			break;
	i += 2;
	SINT	last = mUnknown0C - 1;
	if (i < last)
	{
		remove = last - i - 1;
		for (j = last; j < mUnknown0C; j++)
		{
			mUnknown10[j - remove] = mUnknown10[j];
			mUnknown18[j - remove] = mUnknown18[j];
		}
		mUnknown0C -= remove;
	}

	// and the furthest the first can see
	for (i = 0; i < mUnknown0C; i++)
		if (grid->IsBlocked(mUnknown10[0], mUnknown18[0], mUnknown10[i], mUnknown18[i]))
			break;
	SINT	first = i - 2;
	if (first > 0)
	{
		remove = first - 1;
		for (j = first; j < mUnknown0C; j++)
		{
			mUnknown10[j - remove] = mUnknown10[j];
			mUnknown18[j - remove] = mUnknown18[j];
		}
		mUnknown0C -= remove;
	}
}
