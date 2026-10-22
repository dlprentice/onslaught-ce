// mapwho.h: the map-who grid (only what matched files need so far).

#ifndef MAPWHO_H
#define MAPWHO_H

#include "mapwhoentry.h"

// The eight-byte sector nodes read at 00480b16/00480b25 and 00491d84. A non-null
// child pointer names four node pointers, traversed in order at 00480e1f..00480e43.
class CMapWhoNode
{
public:
	CMapWhoNode();	// 00491900: Create passes this ctor with stride 8 at 00491a44..00491a54
	~CMapWhoNode();	// 00491910: frees the optional child-pointer array
	CMapWhoNode	**mChildren;
	CMapWhoEntry	*mFirst;
};

class CMapWho
{
public:
	~CMapWho() { Clear(); }						// Inlined in the MAP_WHO atexit body at 00491890
	void	SortEntries();						// 004926e0
	class CMapWhoEntry	*GetFirstEntryWithinRadius(FVector pos, float radius);	// 00491ea0 (Stuart's name; the position by value, CRound 004dabda)
	class CMapWhoEntry	*GetNextEntryWithinRadius();							// 00492020 (Stuart's name)
	void	Create();							// 004919b0
	void	Clear();							// 00491930
	void	DrawMapWhoDebug();					// 00492950 (CDXEngine::Render 0053e8ec)
	void	DrawSectorDebug(CSector s);			// 00492860: the sector passed by value (x/y dword, then the layer)
	// Reconstruction names for the following methods; original source identities remain open.
	void	AddEntry(CMapWhoEntry *entry);					// 00491cd0: next/previous links at entry+0/+4
	void	RemoveEntry(CMapWhoEntry *entry);				// 00491d20
	SINT	GetLevelForRadius(float radius);					// 00491c50
	CSector	WorldToSector(const FVector &pos, SINT layer);	// 00492670: hidden output pointer, ret 0x0c
	BOOL	IsValidSector(const CSector &sector);				// 00491da0
	BOOL	SetupNextRadiusLayer();							// 00491df0: decrements the layer and resets its rectangle scan
	CMapWhoEntry	*AdvanceRadiusIterator();				// Reconstruction helper: inlined walks at 00491f4d and 00492042
	CMapWhoEntry	*GetFirstEntryWithinLine(FVector start, FVector end);	// 00492110: two by-value vectors, ret 0x20
	CMapWhoEntry	*GetNextEntryWithinLine();				// 004925a0
	BOOL	SetupNextLineLayer();							// 004922f0
	BOOL	AdvanceLineSector();								// 004924b0
	CMapWhoEntry	*AdvanceLineEntryIterator();				// Reconstruction helper: inlined at 00492233 and 004925c2
	CMapWhoEntry	*GetFirstEntry(CMapWhoNode *node);			// 00491d80: node+4 into the shared iterator
	CMapWhoEntry	*GetNextEntry();							// 00491d90
	CMapWhoNode	*GetSector(const CSector &sector)
	{
		return &mLayers[sector.mLayer][sector.mX + sector.mY * (64 >> (4 - sector.mLayer))];
	}

	CMapWhoEntry	*mIterator;				// +0x00: shared by GetFirstEntry/GetNextEntry
	SINT	mQueryMinX, mQueryMinY;		// +0x04/+0x08: unclamped sector bounds (00491df0)
	SINT	mQueryMaxX, mQueryMaxY;		// +0x0c/+0x10
	FVector	mQueryPos;				// +0x14: copied by value at 00491eb6..00491ec8
	SINT	mQueryLayer;				// +0x24: radius and line scans descend from layer 4 to 0
	float	mQueryRadius;				// +0x28
	CSector	mQuerySector;				// +0x2c: current signed-short X/Y and layer
	SINT	mLineSide;					// +0x34: center, then two perpendicular neighbors (004924b6)
	FVector	mLineStart, mLineEnd;		// +0x38/+0x48: clipped endpoint copies (00492184..004921b3)
	SINT	mLineStep;					// +0x58: completed centerline steps
	CSector	mLineSector;				// +0x5c: current candidate sector
	BOOL	mQueryActive;				// +0x64: shared radius/line flag (00492024/004925a4)
	BOOL	mLineXMajor;				// +0x68: X dominates; equal nonzero deltas choose Y
	float	mLineStepX, mLineStepY;		// +0x6c/+0x70: one-cell step on the dominant axis
	SINT	mLineStepLimit;				// +0x74: floor(dominant delta / cell width) + 2
	FVector	mLinePosition;				// +0x78: starts one step before the clipped start
	CSector	mLineBaseSector;			// +0x88: clamped sector for the current centerline point
	CMapWhoNode	**mLayers;				// +0x90: five layer pointers (00480b0d..00480b16)
	SINT	mShift[5];					// +0x94: per-layer coordinate shifts (00492689)
	SINT	mWidth[5];					// +0xa8: clamp upper bounds (0049269f)
	SINT	mHeight[5];					// +0xbc: clamp upper bounds (004926b3)
	// The rest of the map-who layout is not yet reconstructed.
};

extern CMapWho MAP_WHO;							// 00704200

#endif
