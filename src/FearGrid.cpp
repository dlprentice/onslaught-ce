// FearGrid.cpp: each side's fear grid (reconstructed; Stuart's file is not in his release, his CBattleEngine calls
// InDanger). A cell is feared where the other side's rounds will land and unsafe around this side's own units; the
// AI keeps out of both.

#include	"Common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#define FEARGRID_CPP
#include	"FearGrid.h"
#include	"world.h"
#include	"round.h"
#include	"unit.h"
#include	"map.h"
#include	"eventmanager.h"

// The per-file maths constants from maths.h come first:
// FUNCTION: 0x0044c2e0 init:ZERO_FVECTOR
// FUNCTION: 0x0044c300 init:ID_FMATRIX

#define FEAR_GRID_EVENT_REBUILD		1000
#define FEAR_CELL(v)				(SINT(v) / 8)

//*********************************************************************************
CFearGrid::CFearGrid(SINT allegiance)	// FUNCTION: 0x0044c3d0
{
	mAllegiance = allegiance;
	Rebuild();
}

//*********************************************************************************
void	CFearGrid::HandleEvent(CEvent *event)	// FUNCTION: 0x0044c420
{
	if (event->GetEventNum() == FEAR_GRID_EVENT_REBUILD)
		Rebuild();
}

//*********************************************************************************
void	CFearGrid::Rebuild()	// FUNCTION: 0x0044c440
{
	SINT	x, y;

	for (x = 0; x < FEAR_GRID_SIZE; x++)
	{
		for (y = 0; y < FEAR_GRID_SIZE; y++)
		{
			mFear[y][x] = 0;
			mSafe[y][x] = 1;
		}
	}

	// where the other side's rounds will land
	for (CRound *round = (CRound *)WORLD.mSet60.First(); round; round = (CRound *)WORLD.mSet60.Next())
	{
		if (mAllegiance == round->mAllegiance)
		{
			SINT	r = SINT(round->GetExplosionUnknown34() * 0.125f);
			SINT	y0 = FEAR_CELL(round->GetTargetPos().Y) - r;
			SINT	y1 = FEAR_CELL(round->GetTargetPos().Y) + r;
			SINT	x0 = FEAR_CELL(round->GetTargetPos().X) - r;
			SINT	x1 = FEAR_CELL(round->GetTargetPos().X) + r;

			if ((y0 > 0) && (y1 < FEAR_GRID_SIZE) && (x0 > 0) && (x1 < FEAR_GRID_SIZE))
			{
				for (y = y0; y <= y1; y++)
					for (x = x0; x <= x1; x++)
						mFear[x][y] = 1;
			}
		}
	}

	// around this side's ground units
	for (CUnit *unit = WORLD.mUnitNB.First(); unit; unit = WORLD.mUnitNB.Next())
	{
		if ((!unit->IsA(THING_TYPE_AIR_UNIT)) && (mAllegiance == unit->mAllegiance))
		{
			SINT	y0 = FEAR_CELL(unit->GetPos().Y) - 1;
			SINT	y1 = FEAR_CELL(unit->GetPos().Y) + 1;
			SINT	x0 = FEAR_CELL(unit->GetPos().X) - 1;
			SINT	x1 = FEAR_CELL(unit->GetPos().X) + 1;

			if ((y0 > 0) && (y1 < FEAR_GRID_SIZE) && (x0 > 0) && (x1 < FEAR_GRID_SIZE))
			{
				for (y = y0; y <= y1; y++)
					for (x = x0; x <= x1; x++)
						mSafe[x][y] = 0;
			}
		}
	}

	EVENT_MANAGER.AddEvent(FEAR_GRID_EVENT_REBUILD, this, EVENT_MANAGER.GetTime() + 1.f);
}

//*********************************************************************************
BOOL	CFearGrid::InDanger(FVector pos)	// FUNCTION: 0x0044c720
{
	SINT	x = FEAR_CELL(pos.X);
	SINT	y = FEAR_CELL(pos.Y);
	if ((x >= 0) && (y >= 0) && (x < FEAR_GRID_SIZE) && (y < FEAR_GRID_SIZE))
		return mFear[x][y];
	return FALSE;
}

//*********************************************************************************
BOOL	CFearGrid::Unknown44c780(FVector pos)	// FUNCTION: 0x0044c780
{
	if (pos.Z - MAP.Collide(pos) > -5.f)
	{
		SINT	x = FEAR_CELL(pos.X);
		SINT	y = FEAR_CELL(pos.Y);
		if ((x >= 0) && (y >= 0) && (x < FEAR_GRID_SIZE) && (y < FEAR_GRID_SIZE))
			return mSafe[x][y];
	}
	return TRUE;
}

//*********************************************************************************
// moves pos to the nearest cell nothing is feared in, searching square rings outwards
void	CFearGrid::Unknown44c810(FVector *pos)	// FUNCTION: 0x0044c810
{
	SINT	cx = FEAR_CELL(pos->X);
	SINT	cy = FEAR_CELL(pos->Y);
	if ((cx < 0) || (cy < 0) || (cx >= FEAR_GRID_SIZE) || (cy >= FEAR_GRID_SIZE))
		return;

	for (SINT r = 0; r < FEAR_GRID_SIZE; r++)
	{
		for (SINT y = cy - r; y <= cy + r; y++)
		{
			if ((y >= 0) && (y < FEAR_GRID_SIZE))
			{
				for (SINT x = cx - r; x <= cx + r; x++)
				{
					if ((x >= 0) && (x < FEAR_GRID_SIZE) && (mFear[x][y] == 0))
					{
						if (r > 0)
						{
							pos->X = float(x) * 8.f;
							pos->Y = float(y) * 8.f;
						}
						return;
					}
				}
			}
		}
	}
}
