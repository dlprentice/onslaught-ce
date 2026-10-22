// mapwhoentry.h: a thing's entry in the map-who grid (16 bytes in CThing at +0x0c).

#ifndef MAPWHOENTRY_H
#define MAPWHOENTRY_H
#include "maths.h"

class CThing;

class CSector
{
public:
	CSector() {}
	// Used by the neighbor sweep at 004810dc..004810fb; coordinates are stored as signed shorts.
	CSector(SINT x, SINT y, SINT layer) : mX(x), mY(y), mLayer(layer) {}
	// member-wise, as retail copies it (two WORD moves and a DWORD, 00401924)
	CSector	&operator=(const CSector &o) { mX = o.mX; mY = o.mY; mLayer = o.mLayer; return *this; }
	// The short-circuit coordinate/layer comparison at 0048109b..004810bd.
	BOOL	operator==(const CSector &o) const { return mX == o.mX && mY == o.mY && mLayer == o.mLayer; }
#ifdef HLCOLLISIONDETECTOR_CPP	// MovedSector steps both sectors up a layer through them (00481287..004812a9)
	void	SetX(SINT x) { mX = x; }
	void	SetY(SINT y) { mY = y; }
	void	SetLayer(SINT l) { mLayer = l; }
#endif
	// Reconstruction name: the paired parent-layer steps at 00481287..004812a9. The layer steps first, as MovedSector's
	// SetLayer does: stepping x and y first lets VC6 derive a counter from `4 - layer` in CMapWho::Create and drop the
	// parent's layer store, which retail keeps.
	void	ToParent() { mLayer--; mX >>= 1; mY >>= 1; }
	// Signed coordinate reads at 004810ca and 004810eb.
	SINT	GetX() { return mX; }
	SINT	GetY() { return mY; }
	SINT	GetLayer() { return mLayer; }
	short	mX, mY;
	SINT	mLayer;
};

class CMapWhoEntry
{
public:
	CMapWhoEntry();									// 00492c60: constructor, returns this in EAX
	~CMapWhoEntry();								// MapWhoEntry.cpp: expands RemoveFromMap, so the linker folds the two (00492c70)
	void		RemoveFromMap();							// 00492c70
	BOOL		UpdateEntry(const FVector &old_pos);		// 00492ca0
	void		Init(const FVector &pos, CThing *thing, float force_radius);
	CSector		&GetSectorIn() { return mSectorIn; }
	CThing		*GetThing();								// 00492c90 (Stuart's name): the thing that holds the entry, 0xc before it

	// AddEntry (00491cd0) and RemoveEntry (00491d20) link entries; ownership is this - 0xc.
	CMapWhoEntry *mNext;				// +0x00
	CMapWhoEntry *mPrev;				// +0x04
	CSector		mSectorIn;				// +0x08 (layer at +0x0c)
};

#endif
