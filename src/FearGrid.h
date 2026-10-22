// FearGrid.h: each side's fear grid (only what matched files need so far). CGame::InitRestartLoop makes one per
// allegiance, 0x800c bytes each (0046c604); RTTI: CFearGrid (vtable 005db2a4) derives from CMonitor.

#ifndef FEARGRID_H
#define FEARGRID_H
#include "maths.h"

#include "Monitor.h"

#define FEAR_GRID_SIZE		64				// cells a side, 8 units each

class CFearGrid : public CMonitor
{
public:
	CFearGrid(SINT allegiance);					// 0044c3d0
	BOOL	InDanger(FVector pos);					// 0044c720 (Stuart's name; by value, CBattleEngine::TestDanger 0040ddd0)
	BOOL	Unknown44c780(FVector pos);			// 0044c780: by-value vector, ret 0x10; returns a grid dword or 1
	void	Unknown44c810(FVector *pos);			// 0044c810: writes X/Y; exits leave unrelated incidental EAX values

#ifdef FEARGRID_CPP	// FearGrid.cpp's view; the other objects keep theirs (declaration counts)
	virtual void	HandleEvent(CEvent *event);		// 0 (0044c420): event 1000 rebuilds
	void	Rebuild();								// 0044c440: then again a second later

	SINT	mFear[FEAR_GRID_SIZE][FEAR_GRID_SIZE];	// +0x0008: [x][y], set where the other side's rounds will land
	SINT	mSafe[FEAR_GRID_SIZE][FEAR_GRID_SIZE];	// +0x4008: [x][y], cleared around this side's units
	SINT	mAllegiance;							// +0x8008
#else
	UBYTE	mUnknown0008[0x800c - 0x8];
#endif
};

#endif
