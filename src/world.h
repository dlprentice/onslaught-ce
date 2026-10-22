// world.h: the world, the owner of every thing. WORLD is at 00855090: the CWorld members are all called with that
// this pointer, and its set of every thing is its first member. Stuart's release has no world.h; the layout is read
// from world.obj (constructor 0050a9c0, destructor 0050a870, Clear 0050ada0) and the names from Stuart's callers
// (WORLD.GetNamedThingNB, GetObjectiveThingNB, GetBigThingsNB, GetCopyOfScript, IsMultiplayer, GetType, Render).
// thing.obj reaches the notice boards at fixed offsets: named things +0xa0 (00855130), objective things +0xb0
// (00855140) and big things +0xe0 (00855170). The sets without a name are ordered by their offsets.

#ifndef WORLD_H
#define WORLD_H

#include "SPtrSet.h"
#include "WorldPhysicsManager.h"
#include "thing.h"
#include "activereader.h"

class CThing;
class CComplexThing;
class CMissionScriptObjectCode;
class CStringDataType;
class CGenericActiveReader;
class CMonitor;
class CDXMemBuffer;
class CChunkReader;
class C3DGamut;
class CUnit;
class CSquad;
class CSpawnPoint;
class CNormalSquadUnknownD4;

// A mission script by name (8 bytes, MT_SCRIPT): the world's scripts are read by LoadScripts (0050ac70) and
// deleted by the set's DeleteAll (0050d7f0), whose inlined destructor deletes and clears both members.
// A line test's result (Stuart's name and members, BattleEngine.cpp; 16 bytes: CBattleEngine's mWlcr sits
// between the crosshair readers and mAutoAimTarget, which puts mWalkerPart at +0x578 as retail has it).
class CWorldLineColReport
{
public:
	// FindFirstThingToHitLine stores the thing, part, type and distance at 0050b4da..0050b4e6.
	// Its constructor clears both +0 and +8; those identical zero stores alone cannot establish their types.
	CWorldLineColReport() { mHitThing = NULL; mUnknown04 = -1; mHitType = kCollideNothing; mDistToImpact = -1.0f; }

	CThing			*mHitThing;			// +0x00 (Stuart's)
	SINT			mUnknown04;			// +0x04
	ECollisionType	mHitType;			// +0x08
	float			mDistToImpact;		// +0x0c
};

class CScriptObjectCodeEntry
{
public:
	CScriptObjectCodeEntry(CStringDataType *name, CMissionScriptObjectCode *code) { mName = name; mCode = code; }
	~CScriptObjectCodeEntry()
	{
		delete mName; mName = NULL;
		delete mCode; mCode = NULL;
	}

	CStringDataType				*mName;				// +0x00
	CMissionScriptObjectCode	*mCode;				// +0x04
};

// The occupancy grids the world keeps (0x2004 bytes, MEMTYPE_NAVIGATION; InitOccupancy 0050d580). Its constructor
// takes the cell size (004bc260), its destructor is 004bc2d0. CWorld::Clear's deletes call the scalar deleting
// destructor world.obj emits (0050d680): the inline budget was spent before them.
class COccupancyGrid
{
public:
	COccupancyGrid(float cell_size);
	~COccupancyGrid();
	void	Unknown4bc2e0(FVector start, FVector end, SINT a, CNormalSquadUnknownD4 *path);	// 004bc2e0, ret 0x28; 004be1d0 writes path, with no coherent EAX result
	// World coordinates address two-unit cells (004e7903, 004e7ad8, 004e7e5c). A reference preserves
	// the caller-owned position in GroundVehicleGuide 0047d8c0..0047d9d0; no outlined ABI was recovered.
	BOOL	UnknownCell(const FVector &pos)
	{
		SINT x = (SINT)pos.X >> 1;
		SINT y = (SINT)pos.Y >> 1;
		return mData[(x >> 3) * 256 + y] & (1 << (x % 8));
	}

#ifdef NAVIGATION_FULL	// NavigationMap.cpp's view; world.h's other includers keep the byte array
	// World coordinates (two units a cell). Inline, defined in NavigationMap.cpp after COccupancy::BuildStaticShadows:
	// ClearCross expands it, and retail emits its out-of-line copy there (004bdf70).
	inline void	SetCell(SINT x, SINT y, BOOL set);
	// A cell and its four neighbours, world coordinates. Inline: the occupancy builders expand it (004bc964), and its
	// out-of-line copy (004bd440) follows its first caller that could not. The + 0 terms are retail's inline cost:
	// without them Rasterize and BuildStatic expand one ClearCross more than retail.
	void	ClearCross(SINT x, SINT y)
	{
		SetCell(x - 1, y + 0, FALSE);
		SetCell(x + 0, y - 1, FALSE);
		SetCell(x + 0, y + 0, FALSE);
		SetCell(x + 1, y + 0, FALSE);
		SetCell(x + 0, y + 1, FALSE);
	}
	void	SkipLegacy(class CDXMemBuffer &file);							// 004bdff0
	void	Load(class CDXMemBuffer &file);								// 004be050
	static void	SkipHeader(class CDXMemBuffer &file);						// 004be170
	SINT	TestCell(SINT x, SINT y) { return (1 << (x % 8)) & mData[(x >> 3) * 256 + y]; }	// 004be970: grid coordinates
	BOOL	IsBlocked(SINT x0, SINT y0, SINT x1, SINT y1);					// 004bc510: grid coordinates
	static void	StepDown(SINT *x, SINT *y);									// 004bed30: to the cheapest of the eight neighbours
	static void	BuildPath(FVector start, FVector end, COccupancyGrid *grid, SINT a, CNormalSquadUnknownD4 *path);	// 004be1d0 (cdecl)
	BOOL	FindNearest(SINT *x, SINT *y);									// 004bc6d0: the nearest open cell to one that is blocked
	static BOOL	Flood();													// 004be420: fills sCost out from (sX, sY) until it reaches the end
	static void	FindNearestVisited();										// 004beb30: when the flood cannot reach the end
	// The search may step to a neighbour of (sX, sY) that is open and unvisited. Inline: Flood expands the first eight
	// and calls the rest (004be9b0, 004bea10, 004bea70, 004bead0); without the else it expands nine.
	static BOOL	CanStepNorth() { if (sX > 0 && sCost[sX - 1][sY] == 0xffff && sGrid->TestCell(sX - 1, sY)) return TRUE; else return FALSE; }
	static BOOL	CanStepWest() { if (sY > 0 && sCost[sX][sY - 1] == 0xffff && sGrid->TestCell(sX, sY - 1)) return TRUE; else return FALSE; }
	static BOOL	CanStepSouth() { if (sX < 255 && sCost[sX + 1][sY] == 0xffff && sGrid->TestCell(sX + 1, sY)) return TRUE; else return FALSE; }
	static BOOL	CanStepEast() { if (sY < 255 && sCost[sX][sY + 1] == 0xffff && sGrid->TestCell(sX, sY + 1)) return TRUE; else return FALSE; }

	UBYTE			mData[0x2000];											// +0x0000: bit x & 7 of byte (x >> 3) * 256 + y
	float			mMaxSlope;												// +0x2000: radians

	static SINT		sEndX, sEndY;											// 00809db4, 00809db0: the search's end cell
	static COccupancyGrid	*sGrid;											// 00809db8: the search's grid
	static SINT		sY;														// 00809dbc
	static UWORD	sCost[256][256];										// 00809dc0: 0xffff unvisited
	static SINT		sX;														// 00829dc0
	static SINT		sMinX, sMinY;											// 00829dc4, 00829dc8: the touched costs' bounds
	static SINT		sMaxX, sMaxY;											// 00630ab4, 00630ab8
	static BOOL		sEnabled;												// 00809598
	static SINT		sMoves[500];											// 0080959c: the step Flood took from each depth
	static SPtrSet<CUnit>	sUnits;											// 00809588: the units on the grids
#else
#ifdef WORLD_CPP	// CWorld::LoadWorld reads or skips the grids (0050d331, 0050d363, 0050d386)
	void	SkipLegacy(class CDXMemBuffer &file);							// 004bdff0
	void	Load(class CDXMemBuffer &file);								// 004be050
	static void	SkipHeader(class CDXMemBuffer &file);						// 004be170
#endif
	UBYTE			mData[0x2004];
#endif
};

#define MAX_WORLD_TEXT_SLOTS	4
#define NUM_WORLD_COUNTERS		26

class CStart;

class CLine;

class CWorld
{
public:
	// Stuart's (BattleEngine.cpp): the first thing along a line (00050b030; CBattleEngine::Move passes the line by
	// value, then the thing to ignore, the report and six flags, the last -1: 004094f5..00409549).
	ECollisionType	FindFirstThingToHitLine(CLine line, CThing *ignore, CWorldLineColReport *report, BOOL firstOnly = FALSE, BOOL meshCollision = TRUE, int level = 2, ULONG ignore_types = 0, BOOL mapFlag = FALSE, int allowedTypes = -1);	// 0050b030, returns report+8; ret 0x54 (the defaults but the last are not checked yet)

	enum EWorldType
	{
		kSinglePlayer,
		kCooperativeMultiplayer,
		kVersusMultiplayer
	};

	CWorld();												// 0050a9c0
	~CWorld();												// 0050a870

	void						Render(C3DGamut *gamut, BOOL imposters, BOOL blob_shadows);	// 0050ab60
	void						Shutdown();					// 0050abb0
	CMissionScriptObjectCode	*GetCopyOfScript(char *name);	// 0050abc0
	void						LoadScripts(CDXMemBuffer *file);	// 0050ac70
	void						Clear();					// 0050ada0
	CComplexThing				*FindThingByName(char *name);	// 0050af70
	void						AddToOccupancy(CUnit *unit);		// 0050b010
	void						RemoveFromOccupancy(CUnit *unit);	// 0050b020
	BOOL						Load(SINT level, BOOL base = FALSE);	// 0050b520
	void						Deserialize(CChunkReader *c);	// 0050b780
	BOOL						LoadWorld(CDXMemBuffer *file, BOOL base, BOOL real);	// 0050b9c0
	void						LoadHeader(CDXMemBuffer *file, BOOL skip);	// 0050d4c0
	void						InitOccupancy();			// 0050d580
	void						AddText(DWORD id, SINT state);	// 0050d6a0
	void						SetTextTime(DWORD id, float time, float duration);	// 0050d720
	float						GetTextTime(SINT slot);		// 0050d760
	void						RemoveText(DWORD id);		// 0050d7a0
	SPtrSet<CActiveReader<CThing> >&	GetBaseWorldThingNB() { return mBaseWorldThingNB; }
	SPtrSet<CStart>&			GetStartNB() { return mStartNB; }
	SPtrSet<CSpawnPoint>&		GetSpawnPointNB() { return mSpawnPointNB; }
	SPtrSet<CUnit>&				GetUnitNB() { return mUnitNB; }
	SPtrSet<CSquad>&			GetSquadNB() { return mSquadNB; }
	// Stuart's: nothing loaded (CGame::RunLevel 0046e2eb..0046e2fd)
	void						ResetLoadedState() { mWorldLevel = -1; mBaseWorldLevel = -1; mWorldResourcePos = -1; mBaseWorldResourcePos = -1; }
	BOOL						IsMultiplayer();			// 0050d7d0
	EWorldType					GetType()							{ return mType; }

	void						AddThing(CThing *thing)				{ mThings.Add(thing); }
	void						RemoveThing(CThing *thing)			{ mThings.Remove(thing); }
	SPtrSet<CThing>&			GetThingNB()						{ return mThings; }
	SPtrSet<CComplexThing>&		GetNamedThingNB()					{ return mNamedThings; }
	SPtrSet<CThing>&			GetObjectiveThingNB()				{ return mObjectiveThings; }
	SPtrSet<CThing>&			GetBigThingsNB()					{ return mBigThings; }

	SPtrSet<CThing>				mThings;					// +0x00
	SPtrSet<CSquad>				mSquadNB;					// +0x10 (Stuart's; CGame::ToggleDebugSquadForward 0046fe47)
	SPtrSet<CThing>				mSet20;						// +0x20
	SPtrSet<CThing>				mSet30;						// +0x30
	SPtrSet<CUnit>				mUnitNB;					// +0x40 (Stuart's; CGame::RestartLoopRunLevel 0046e072)
	SPtrSet<CThing>				mSet50;						// +0x50
	SPtrSet<CThing>				mSet60;						// +0x60
	SPtrSet<CStart>				mStartNB;					// +0x70 (Stuart's; CGame::PostLoadProcess 0046d0a9)
	SPtrSet<CSpawnPoint>		mSpawnPointNB;				// +0x80 (Stuart's; CGame::RespawnPlayer 00470216)
	SPtrSet<CThing>				mWaypoints;					// +0x90 (00855120): every waypoint, in creation order (CWaypoint::Init, Shutdown).
															// Typed as CThing: an SPtrSet<CWaypoint> here moves CGame::DrawDebugStuff's and
															// CFEPGoodies::Render's declaration-order ties
	SPtrSet<CComplexThing>		mNamedThings;				// +0xa0
	SPtrSet<CThing>				mObjectiveThings;			// +0xb0
	SPtrSet<CActiveReader<CThing> >	mBaseWorldThingNB;	// +0xc0 (Stuart's): deleted by Clear; CGame::FillOutEndLevelData
	SPtrSet<CThing>				mSetD0;						// +0xd0
	SPtrSet<CThing>				mBigThings;					// +0xe0
	SPtrSet<CMonitor>			mOwnedF0;					// +0xf0: deleted by Clear
	SPtrSet<CMonitor>			mOwned100;					// +0x100: deleted by Clear
	SPtrSet<CThing>				mSet110;					// +0x110
	SPtrSet<CScriptObjectCodeEntry>	mScripts;				// +0x120
	SINT						mCountersA[NUM_WORLD_COUNTERS];	// +0x130
	SINT						mCountersB[NUM_WORLD_COUNTERS];	// +0x198
	COccupancyGrid				*mOccupancy[3];				// +0x200
	SINT						mTextState[MAX_WORLD_TEXT_SLOTS];	// +0x20c
	DWORD						mTextId[MAX_WORLD_TEXT_SLOTS];		// +0x21c
	WCHAR						*mText[MAX_WORLD_TEXT_SLOTS];		// +0x22c
	float						mTextTime[MAX_WORLD_TEXT_SLOTS];	// +0x23c
	float						mTextDuration[MAX_WORLD_TEXT_SLOTS];	// +0x24c
	float						mTextStart[MAX_WORLD_TEXT_SLOTS];	// +0x25c
	// The levels whose worlds are loaded, and where each world's chunk starts in the resource file (-1: none).
	SINT						mWorldLevel;				// +0x26c
	SINT						mBaseWorldLevel;			// +0x270
	SINT						mWorldResourcePos;			// +0x274
	SINT						mBaseWorldResourcePos;		// +0x278
	EWorldType					mType;						// +0x27c
};

extern CWorld WORLD;

#include "squad.h"		// CSquad, for the squad set's users (CGame's debug squad toggles)

#endif
